// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <map>
#include <string>
#include <utility>
#include <chrono>
#include <boost/assign.hpp>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/common/wire_protocol.h"

#include "yb/cdc/cdc_service.h"
#include "yb/client/client.h"
#include "yb/client/client-test-util.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/master/master.pb.h"
#include "yb/master/master-test-util.h"

#include "yb/master/cdc_consumer_registry_service.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/tserver/cdc_consumer.h"
#include "yb/util/atomic.h"
#include "yb/util/faststring.h"
#include "yb/util/random.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"

DECLARE_int32(replication_factor);
DECLARE_bool(mock_get_changes_response_for_consumer_testing);
DECLARE_bool(twodc_write_hybrid_time_override);

namespace yb {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBError;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTable;
using client::YBTableAlterer;
using client::YBTableCreator;
using client::YBTableType;
using client::YBTableName;
using master::MiniMaster;
using tserver::MiniTabletServer;
using tserver::enterprise::CDCConsumer;

namespace enterprise {

constexpr int kRpcTimeout = 30;
static const std::string kUniverseId = "test_universe";
static const std::string kNamespaceName = "test_namespace";

class TwoDCTest : public YBTest {
 public:
  Result<std::vector<std::shared_ptr<client::YBTable>>>
      SetUpWithParams(std::vector<uint32_t> num_consumer_tablets,
                      std::vector<uint32_t> num_producer_tablets,
                      uint32_t replication_factor) {
    YBTest::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = replication_factor;
    FLAGS_replication_factor = replication_factor;
    opts.cluster_id = "producer";
    producer_cluster_ = std::make_unique<MiniCluster>(Env::Default(), opts);
    RETURN_NOT_OK(producer_cluster_->StartSync());
    RETURN_NOT_OK(producer_cluster_->WaitForTabletServerCount(replication_factor));

    opts.cluster_id = "consumer";
    consumer_cluster_ = std::make_unique<MiniCluster>(Env::Default(), opts);
    RETURN_NOT_OK(consumer_cluster_->StartSync());
    RETURN_NOT_OK(consumer_cluster_->WaitForTabletServerCount(replication_factor));

    producer_client_ = VERIFY_RESULT(producer_cluster_->CreateClient());
    consumer_client_ = VERIFY_RESULT(consumer_cluster_->CreateClient());

    RETURN_NOT_OK(clock_->Init());
    producer_txn_mgr_.emplace(producer_client_.get(), clock_, client::LocalTabletFilter());
    consumer_txn_mgr_.emplace(consumer_client_.get(), clock_, client::LocalTabletFilter());

    YBSchemaBuilder b;
    b.AddColumn("c0")->Type(INT32)->NotNull()->HashPrimaryKey();

    // Create transactional table.
    TableProperties table_properties;
    table_properties.SetTransactional(true);
    b.SetTableProperties(table_properties);
    CHECK_OK(b.Build(&schema_));

    if (num_consumer_tablets.size() != num_producer_tablets.size()) {
      return STATUS(IllegalState,
                    Format("Num consumer tables: $0 num producer tables: $1 must be equal.",
                           num_consumer_tablets.size(), num_producer_tablets.size()));
    }

    std::vector<YBTableName> tables;
    std::vector<std::shared_ptr<client::YBTable>> yb_tables;
    for (int i = 0; i < num_consumer_tablets.size(); i++) {
      RETURN_NOT_OK(CreateTable(i, num_producer_tablets[i], producer_client_.get(), &tables));
      std::shared_ptr<client::YBTable> producer_table;
      RETURN_NOT_OK(producer_client_->OpenTable(tables[i * 2], &producer_table));
      yb_tables.push_back(producer_table);

      RETURN_NOT_OK(CreateTable(i, num_consumer_tablets[i], consumer_client_.get(), &tables));
      std::shared_ptr<client::YBTable> consumer_table;
      RETURN_NOT_OK(consumer_client_->OpenTable(tables[(i * 2) + 1], &consumer_table));
      yb_tables.push_back(consumer_table);
    }

    return yb_tables;
  }

  Result<YBTableName> CreateTable(YBClient* client, const std::string& namespace_name,
                                  const std::string& table_name, uint32_t num_tablets) {
    YBTableName table = YBTableName(namespace_name, table_name);
    RETURN_NOT_OK(client->CreateNamespaceIfNotExists(table.namespace_name()));

    // Add a table, make sure it reports itself.
    gscoped_ptr<YBTableCreator> table_creator(client->NewTableCreator());
        RETURN_NOT_OK(table_creator->table_name(table)
                          .schema(&schema_)
                          .table_type(YBTableType::YQL_TABLE_TYPE)
                          .num_tablets(num_tablets)
                          .Create());
    return table;
  }

  Status CreateTable(
      uint32_t idx, uint32_t num_tablets, YBClient* client, std::vector<YBTableName>* tables) {
    auto table = VERIFY_RESULT(CreateTable(client, kNamespaceName, Format("test_table_$0", idx),
                                           num_tablets));
    tables->push_back(table);
    return Status::OK();
  }

  Status SetupUniverseReplication(
      MiniCluster* producer_cluster, MiniCluster* consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, const std::vector<std::shared_ptr<client::YBTable>>& tables) {
    master::SetupUniverseReplicationRequestPB req;
    master::SetupUniverseReplicationResponsePB resp;

    req.set_producer_id(universe_id);
    string master_addr = producer_cluster->GetMasterAddresses();
    auto hp_vec = VERIFY_RESULT(HostPort::ParseStrings(master_addr, 0));
    HostPortsToPBs(hp_vec, req.mutable_producer_master_addresses());

    req.mutable_producer_table_ids()->Reserve(tables.size());
    for (const auto& table : tables) {
      req.add_producer_table_ids(table->id());
    }

    auto master_proxy = std::make_shared<master::MasterServiceProxy>(
        &consumer_client->proxy_cache(),
        consumer_cluster->leader_mini_master()->bound_rpc_addr());

    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));
    RETURN_NOT_OK(master_proxy->SetupUniverseReplication(req, &resp, &rpc));
    if (resp.has_error()) {
      return STATUS(IllegalState, "Failed setting up universe replication");
    }
    return Status::OK();
  }

  Status VerifyUniverseReplication(
      MiniCluster *consumer_cluster, YBClient* consumer_client,
      const std::string& universe_id, master::GetUniverseReplicationResponsePB* resp) {
    return LoggedWaitFor([=]() -> Result<bool> {
      master::GetUniverseReplicationRequestPB req;
      req.set_producer_id(universe_id);
      resp->Clear();

      auto master_proxy = std::make_shared<master::MasterServiceProxy>(
          &consumer_client->proxy_cache(),
          consumer_cluster->leader_mini_master()->bound_rpc_addr());
      rpc::RpcController rpc;
      rpc.set_timeout(MonoDelta::FromSeconds(kRpcTimeout));

      Status s = master_proxy->GetUniverseReplication(req, resp, &rpc);
      return s.ok() && !resp->has_error();
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify universe replication");
  }

  Status GetCDCStreamForTable(
      const std::string& table_id, master::ListCDCStreamsResponsePB* resp) {
    return LoggedWaitFor([=]() -> Result<bool> {
      master::ListCDCStreamsRequestPB req;
      req.set_table_id(table_id);
      resp->Clear();

      Status s = producer_cluster_->leader_mini_master()->master()->catalog_manager()->
          ListCDCStreams(&req, resp);
      return s.ok() && !resp->has_error() && resp->streams_size() == 1;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Get CDC stream for table");
  }

  void Destroy() {
    if (consumer_cluster_) {
      consumer_cluster_->Shutdown();
      consumer_cluster_.reset();
    }

    if (producer_cluster_) {
      producer_cluster_->Shutdown();
      producer_cluster_.reset();
    }

    producer_client_.reset();
    consumer_client_.reset();
  }

  void WriteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table,
                     bool delete_op = false) {
    auto session = client->NewSession();
    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    for (uint32_t i = start; i < end; i++) {
      auto op = delete_op ? table_handle.NewDeleteOp() : table_handle.NewInsertOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ASSERT_OK(session->ApplyAndFlush(op));
    }
  }

  void WriteTransactionalWorkload(uint32_t start, uint32_t end, YBClient* client,
                                  client::TransactionManager* txn_mgr, const YBTableName& table) {
    auto session = client->NewSession();
    auto transaction = std::make_shared<client::YBTransaction>(txn_mgr);
    ReadHybridTime read_time;
    ASSERT_OK(transaction->Init(IsolationLevel::SNAPSHOT_ISOLATION, read_time));
    session->SetTransaction(transaction);

    client::TableHandle table_handle;
    ASSERT_OK(table_handle.Open(table, client));
    std::vector<std::shared_ptr<client::YBqlOp>> ops;

    for (uint32_t i = start; i < end; i++) {
      auto op = table_handle.NewDeleteOp();
      int32_t key = i;
      auto req = op->mutable_request();
      QLAddInt32HashValue(req, key);
      ASSERT_OK(session->ApplyAndFlush(op));
    }
    transaction->CommitFuture().get();
  }

  void DeleteWorkload(uint32_t start, uint32_t end, YBClient* client, const YBTableName& table) {
    WriteWorkload(start, end, client, table, true /* delete_op */);
  }

  std::vector<string> ScanToStrings(const YBTableName& table_name, YBClient* client) {
    client::TableHandle table;
    EXPECT_OK(table.Open(table_name, client));
    auto result = ScanTableToStrings(table);
    std::sort(result.begin(), result.end());
    return result;
  }


  Status VerifyWrittenRecords(const YBTableName& producer_table,
                              const YBTableName& consumer_table) {
    return LoggedWaitFor([=]() -> Result<bool> {
      auto producer_results = ScanToStrings(producer_table, producer_client_.get());
      auto consumer_results = ScanToStrings(consumer_table, consumer_client_.get());
      return producer_results == consumer_results;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify written records");
  }

  Status VerifyNumRecords(const YBTableName& table, YBClient* client, int expected_size) {
    return LoggedWaitFor([=]() -> Result<bool> {
      auto results = ScanToStrings(table, client);
      return results.size() == expected_size;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Verify number of records");
  }

  Status InitCDCConsumer() {
    master::ListTablesRequestPB tables_req;
    master::ListTablesResponsePB tables_resp;
    tables_req.set_exclude_system_tables(true);

    RETURN_NOT_OK(consumer_cluster_->leader_mini_master()->master()->catalog_manager()->
                  ListTables(&tables_req, &tables_resp));

    auto master_addrs = producer_cluster_->GetMasterAddresses();
    auto consumer_info = VERIFY_RESULT(
        master::enterprise::TEST_GetConsumerProducerTableMap(master_addrs, tables_resp));
    auto universe_uuid = "universe_uuid";

    return consumer_cluster_->leader_mini_master()->master()->catalog_manager()->InitCDCConsumer(
        consumer_info, master_addrs, universe_uuid);
  }

  YBClient* producer_client() {
    return producer_client_.get();
  }

  YBClient* consumer_client() {
    return consumer_client_.get();
  }

  MiniCluster* producer_cluster() {
    return producer_cluster_.get();
  }

  MiniCluster* consumer_cluster() {
    return consumer_cluster_.get();
  }

  client::TransactionManager* producer_txn_mgr() {
    return producer_txn_mgr_.get_ptr();
  }

  client::TransactionManager* consumer_txn_mgr() {
    return consumer_txn_mgr_.get_ptr();
  }

  uint32_t NumProducerTabletsPolled(MiniCluster* cluster) {
    uint32_t size = 0;
    for (const auto& mini_tserver : cluster->mini_tablet_servers()) {
      uint32_t new_size = 0;
      auto* tserver = dynamic_cast<tserver::enterprise::TabletServer*>(
          mini_tserver->server());
      CDCConsumer* cdc_consumer;
      if (tserver && (cdc_consumer = tserver->GetCDCConsumer())) {
        auto tablets_running = cdc_consumer->TEST_producer_tablets_running();
        new_size = tablets_running.size();
      }
      size += new_size;
    }
    return size;
  }

  Status CorrectlyPollingAllTablets(MiniCluster* cluster, uint32_t num_producer_tablets) {
    return LoggedWaitFor([=]() -> Result<bool> {
      static int i = 0;
      constexpr int kNumIterationsWithCorrectResult = 5;
      if (NumProducerTabletsPolled(cluster) == num_producer_tablets) {
        if (i++ == kNumIterationsWithCorrectResult) {
          i = 0;
          return true;
        }
      } else {
        i = 0;
      }
      return false;
    }, MonoDelta::FromSeconds(kRpcTimeout), "Num producer tablets being polled");
  }

  std::unique_ptr<MiniCluster> producer_cluster_;
  std::unique_ptr<MiniCluster> consumer_cluster_;

 private:

  std::unique_ptr<YBClient> producer_client_;
  std::unique_ptr<YBClient> consumer_client_;

  boost::optional<client::TransactionManager> producer_txn_mgr_;
  boost::optional<client::TransactionManager> consumer_txn_mgr_;
  server::ClockPtr clock_{new server::HybridClock()};

  YBSchema schema_;
};

TEST_F(TwoDCTest, SetupUniverseReplication) {
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, 3));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer tables from the list.
  producer_tables.reserve(tables.size() / 2);
  for (int i = 0; i < tables.size(); i += 2) {
    producer_tables.push_back(tables[i]);
  }
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.producer_id(), kUniverseId);
  ASSERT_EQ(resp.producer_tables_size(), producer_tables.size());
  for (int i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.producer_tables(i).table_id(), producer_tables[i]->id());
  }

  // Verify that CDC streams were created on producer for all tables.
  for (int i = 0; i < producer_tables.size(); i++) {
    master::ListCDCStreamsResponsePB stream_resp;
    ASSERT_OK(GetCDCStreamForTable(producer_tables[i]->id(), &stream_resp));
    ASSERT_EQ(stream_resp.streams_size(), 1);
    ASSERT_EQ(stream_resp.streams(0).table_id(), producer_tables[i]->id());
  }

  Destroy();
}

// Test for #2250 to verify that replication for tables with the same prefix gets set up correctly.
TEST_F(TwoDCTest, SetupUniverseReplicationMultipleTables) {
  // Setup the two clusters without any tables.
  auto tables = ASSERT_RESULT(SetUpWithParams({}, {}, 1));

  // Create tables with the same prefix.
  std::string table_names[2] = {"table", "table_index"};

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  for (int i = 0; i < 2; i++) {
    auto t = ASSERT_RESULT(CreateTable(producer_client(), kNamespaceName, table_names[i], 3));
    std::shared_ptr<client::YBTable> producer_table;
    ASSERT_OK(producer_client()->OpenTable(t, &producer_table));
    producer_tables.push_back(producer_table);
  }

  for (int i = 0; i < 2; i++) {
    ASSERT_RESULT(CreateTable(consumer_client(), kNamespaceName, table_names[i], 3));
  }

  // Setup universe replication on both these tables.
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // Verify that universe was setup on consumer.
  master::GetUniverseReplicationResponsePB resp;
  ASSERT_OK(VerifyUniverseReplication(consumer_cluster(), consumer_client(), kUniverseId, &resp));
  ASSERT_EQ(resp.producer_id(), kUniverseId);
  ASSERT_EQ(resp.producer_tables_size(), producer_tables.size());
  for (int i = 0; i < producer_tables.size(); i++) {
    ASSERT_EQ(resp.producer_tables(i).table_id(), producer_tables[i]->id());
  }

  Destroy();
}

TEST_F(TwoDCTest, PollWithConsumerRestart) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, replication_factor));

  FLAGS_mock_get_changes_response_for_consumer_testing = true;
  ASSERT_OK(InitCDCConsumer());

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  consumer_cluster_->mini_tablet_server(0)->Shutdown();

  // After shutting down a consumer node.
  if (replication_factor > 1) {
    ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));
  }

  ASSERT_OK(consumer_cluster_->mini_tablet_server(0)->Start());

  // After restarting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  ASSERT_OK(consumer_cluster_->RestartSync());

  // After consumer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  Destroy();
}

TEST_F(TwoDCTest, PollWithProducerRestart) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({8, 4, 4, 12}, {8, 4, 12, 8}, replication_factor));

  FLAGS_mock_get_changes_response_for_consumer_testing = true;
  ASSERT_OK(InitCDCConsumer());

  // After creating the cluster, make sure all 32 tablets being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  producer_cluster_->mini_tablet_server(0)->Shutdown();

  // After stopping a producer node.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  ASSERT_OK(producer_cluster_->mini_tablet_server(0)->Start());

  // After starting the node.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  ASSERT_OK(producer_cluster_->RestartSync());

  // After producer cluster restart.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 32));

  Destroy();
}

TEST_F(TwoDCTest, ApplyOperations) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  WriteWorkload(0, 5, producer_client(), tables[0]->name());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  Destroy();
}

TEST_F(TwoDCTest, ApplyOperationsWithTransactions) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  // tables contains both producer and consumer universe tables (alternately).
  // Pick out just the producer table from the list.
  producer_tables.reserve(1);
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Write some transactional rows.
  WriteTransactionalWorkload(0, 5, producer_client(), producer_txn_mgr(), tables[0]->name());

  // Write some non-transactional rows.
  WriteWorkload(6, 10, producer_client(), tables[0]->name());

  // Check that all tablets continue to be polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Verify that both clusters have the same records.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  Destroy();
}

TEST_F(TwoDCTest, TestExternalWriteHybridTime) {
  uint32_t replication_factor = NonTsanVsTsan(3, 1);
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, replication_factor));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));

  // Write 2 rows.
  WriteWorkload(0, 2, producer_client(), tables[0]->name());

  // Ensure that records can be read.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Delete 1 record.
  DeleteWorkload(0, 1, producer_client(), tables[0]->name());

  // Ensure that record is deleted on both universes.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));

  // Delete 2nd record but replicate at a low timestamp (timestamp lower than insertion timestamp).
  FLAGS_twodc_write_hybrid_time_override = true;
  DeleteWorkload(1, 2, producer_client(), tables[0]->name());

  // Verify that record exists on consumer universe, but is deleted from producer universe.
  ASSERT_OK(VerifyNumRecords(tables[0]->name(), producer_client(), 0));
  ASSERT_OK(VerifyNumRecords(tables[1]->name(), consumer_client(), 1));

  Destroy();
}

TEST_F(TwoDCTest, BiDirectionalWrites) {
  auto tables = ASSERT_RESULT(SetUpWithParams({2}, {2}, 1));

  // Setup bi-directional replication.
  std::vector<std::shared_ptr<client::YBTable>> producer_tables;
  producer_tables.push_back(tables[0]);
  ASSERT_OK(SetupUniverseReplication(
      producer_cluster(), consumer_cluster(), consumer_client(), kUniverseId, producer_tables));

  std::vector<std::shared_ptr<client::YBTable>> producer_tables_reverse;
  producer_tables_reverse.push_back(tables[1]);
  ASSERT_OK(SetupUniverseReplication(
      consumer_cluster(), producer_cluster(), producer_client(), kUniverseId,
      producer_tables_reverse));

  // After creating the cluster, make sure all producer tablets are being polled for.
  ASSERT_OK(CorrectlyPollingAllTablets(consumer_cluster(), 2));
  ASSERT_OK(CorrectlyPollingAllTablets(producer_cluster(), 2));

  // Write non-conflicting rows on both clusters.
  WriteWorkload(0, 5, producer_client(), tables[0]->name());
  WriteWorkload(5, 10, consumer_client(), tables[1]->name());

  // Ensure that records are the same on both clusters.
  ASSERT_OK(VerifyWrittenRecords(tables[0]->name(), tables[1]->name()));
  // Ensure that both universes have all 10 records.
  ASSERT_OK(VerifyNumRecords(tables[0]->name(), producer_client(), 10));

  // Write conflicting records on both clusters (1 clusters adds key, another deletes key).
  std::vector<std::thread> threads;
  for (int i = 0; i < 2; ++i) {
    auto client = i == 0 ? producer_client() : consumer_client();
    int index = i;
    bool is_delete = i == 0;
    threads.emplace_back([this, client, index, tables, is_delete] {
      WriteWorkload(10, 20, client, tables[index]->name(), is_delete);
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  // Ensure that same records exist on both universes.
  VerifyWrittenRecords(tables[0]->name(), tables[1]->name());

  Destroy();
}

} // namespace enterprise
} // namespace yb
