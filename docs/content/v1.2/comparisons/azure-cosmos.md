---
title: Azure Cosmos DB
linkTitle: Azure Cosmos DB
description: Azure Cosmos DB
menu:
  v1.2:
    identifier: azure-cosmos
    parent: comparisons
    weight: 1110
---

Yugabyte DB's multi-API and tunable read latency approaches are similar to that of [Azure Cosmos DB](https://azure.microsoft.com/en-us/blog/a-technical-overview-of-azure-cosmos-db/). However, it's underlying storage and replication architecture is much more resilient than that of Cosmos DB. Support for fully distributed SQL and global consistency across multi-region and multi-cloud deployments makes it much more suitable for enterprises building scale-out RDBMS as well as internet-scale OLTP apps but do not want to get locked into a proprietary database such as Cosmos DB.

A post on our blog titled [Practical Tradeoffs in Google Cloud Spanner, Azure Cosmos DB and Yugabyte DB](https://blog.yugabyte.com/practical-tradeoffs-in-google-cloud-spanner-azure-cosmos-db-and-yugabyte-db/) goes through some of above topics in more detail.
