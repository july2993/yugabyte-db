---
title: Helm Chart
linkTitle: Helm Chart
description: Helm Chart
menu:
  v1.1:
    identifier: helm-chart
    parent: deploy-kubernetes
    weight: 621
isTocNested: true
showAsideToc: true
---

## Introduction

[Helm](https://helm.sh/) is an open source packaging tool that helps install applications and services on Kubernetes. It uses a packaging format called `charts`. Charts are a collection of YAML templates that describe a related set of Kubernetes resources.

## Prerequisites

You must have a Kubernetes cluster that has Helm configured. If you have not installed Helm client and server (aka Tiller) yet, follow the instructions [here](https://docs.helm.sh/using_helm/#installing-helm).

The Yugabyte DB Helm chart documented here has been tested with the following software versions:

- Kubernetes 1.10+
- Helm 2.8.0+
- Yugabyte DB Docker Images 1.1.0+
- Kubernetes nodes where a total of 12 CPU cores and 45 GB RAM can be allocated to Yugabyte DB. This can be 3 nodes with 4 CPU core and 15 GB RAM allocated to Yugabyte DB.
- For optimal performance, ensure to set the appropriate [system limits using `ulimit`](../../manual-deployment/system-config/#setting-ulimits/) on each node in your Kubernetes cluster.

Confirm that your `helm` is configured correctly.

```sh
$ helm version
```

```
Client: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
Server: &version.Version{SemVer:"v2.10.0", GitCommit:"...", GitTreeState:"clean"}
```

## Create Cluster

### Clone Yugabyte DB Project

For creating the cluster, you have to first clone the yugabyte-db project and then create a Yugabyte service account in your Kubernetes cluster.

```sh
$ git clone https://github.com/yugabyte/yugabyte-db.git
```

```sh
$ cd ./yugabyte-db/cloud/kubernetes/helm/
```

```sh
$ kubectl create -f yugabyte-rbac.yaml
```

```
serviceaccount/yugabyte-helm created
clusterrolebinding.rbac.authorization.k8s.io/yugabyte-helm created
```

### Initialize Helm

Initialize `helm` with the service account but use the `--upgrade` flag to ensure that you can upgrade any previous initializations you may have made.

```sh
$ helm init --service-account yugabyte-helm --upgrade --wait
```

```
$HELM_HOME has been configured at /Users/<user>/.helm.

Tiller (the Helm server-side component) has been upgraded to the current version.
Happy Helming!
```

### Install Yugabyte DB

Install Yugabyte DB in the Kubernetes cluster using the command below.

```sh
$ helm install yugabyte --namespace yb-demo --name yb-demo --wait
```

If you are running in a resource-constrained environment or a local environment such as minikube, you will have to change the default resource requirements by using the command below. See next section for a detailed description of these resource requirements.

```sh
$ helm install yugabyte --set resource.master.requests.cpu=0.1,resource.master.requests.memory=0.2Gi,resource.tserver.requests.cpu=0.1,resource.tserver.requests.memory=0.2Gi --namespace yb-demo --name yb-demo
```

### Installing Yugabyte DB with YSQL (beta)
If you wish to enable YSQL (beta) support, install Yugabyte DB with additional parameter as shown below.

```sh
$ helm install yugabyte --wait --namespace yb-demo --name yb-demo --set "enablePostgres=true"
```

If you are running in a resource-constrained environment or a local environment such as minikube, you will have to change the default resource requirements by using the command below. See next section for a detailed description of these resource requirements.

```sh
$ helm install yugabyte --set resource.master.requests.cpu=0.1,resource.master.requests.memory=0.2Gi,resource.tserver.requests.cpu=0.1,resource.tserver.requests.memory=0.2Gi --namespace yb-demo --name yb-demo --set "enablePostgres=true"
```

Initialize the YSQL API (after ensuring that cluster is running - see "Check Cluster Status" below)

```sh
$ kubectl exec -it -n yb-demo yb-tserver-0 bash -- -c "YB_ENABLED_IN_POSTGRES=1 FLAGS_pggate_master_addresses=yb-master-0.yb-masters.yb-demo.svc.cluster.local:7100,yb-master-1.yb-masters.yb-demo.svc.cluster.local:7100,yb-master-2.yb-masters.yb-demo.svc.cluster.local:7100 /home/yugabyte/postgres/bin/initdb -D /tmp/yb_pg_initdb_tmp_data_dir -U postgres"
```

Connect using psql client as shown below.

```sh
$ kubectl exec -n yb-demo -it yb-tserver-0 /home/yugabyte/postgres/bin/psql -- -U postgres -d postgres -h yb-tserver-0.yb-tservers.yb-demo -p 5433
```

## Check Cluster Status

You can check the status of the cluster using various commands noted below.

```sh
$ helm status yb-demo
```

```
LAST DEPLOYED: Fri Oct  5 09:04:46 2018
NAMESPACE: yb-demo
STATUS: DEPLOYED

RESOURCES:
==> v1/Service
NAME          TYPE          CLUSTER-IP      EXTERNAL-IP  PORT(S)                              AGE
yb-tservers   ClusterIP     None            <none>       7100/TCP,9000/TCP,6379/TCP,9042/TCP  7s
yb-masters    ClusterIP     None            <none>       7100/TCP,7000/TCP                    7s
yb-master-ui  LoadBalancer  10.106.132.116  <pending>    7000:30613/TCP                       7s

==> v1/StatefulSet
NAME        DESIRED  CURRENT  AGE
yb-master   3        3        7s
yb-tserver  3        3        7s

==> v1/Pod(related)
NAME          READY  STATUS   RESTARTS  AGE
yb-master-0   0/1    Pending  0         7s
yb-master-1   0/1    Pending  0         7s
yb-master-2   0/1    Pending  0         7s
yb-tserver-0  0/1    Pending  0         7s
yb-tserver-1  0/1    Pending  0         7s
yb-tserver-2  0/1    Pending  0         7s

...
```
Check the pods.

```sh
$ kubectl get pods --namespace yb-demo
```

```
NAME           READY     STATUS    RESTARTS   AGE
yb-master-0    1/1       Running   0          4m
yb-master-1    1/1       Running   0          4m
yb-master-2    1/1       Running   0          4m
yb-tserver-0   1/1       Running   0          4m
yb-tserver-1   1/1       Running   0          4m
yb-tserver-2   1/1       Running   0          4m
```
Check the services.

```sh
$ kubectl get services --namespace yb-demo
```

```
NAME           TYPE           CLUSTER-IP      EXTERNAL-IP   PORT(S)                               AGE
yb-master-ui   LoadBalancer   10.111.34.175   <pending>     7000:31418/TCP                        1m
yb-masters     ClusterIP      None            <none>        7100/TCP,7000/TCP                     1m
yb-tservers    ClusterIP      None            <none>        7100/TCP,9000/TCP,6379/TCP,9042/TCP   1m
```

You can even check the history of the `yb-demo` helm chart.

```sh
$ helm history yb-demo
```

```
REVISION  UPDATED                   STATUS    CHART           DESCRIPTION     
1         Fri Oct  5 09:04:46 2018  DEPLOYED  yugabyte-latest Install complete
```


## Configure Cluster

### CPU, Memory & Replica Count

The default values for the Helm chart are in the `helm/yugabyte/values.yaml` file. The most important ones are listed below. As noted in the Prerequisites section above, the defaults are set for a 3 nodes Kubernetes cluster each with 4 CPU cores and 15 GB RAM.

```
persistentVolume:
  count: 2
  storage: 10Gi
  storageClass: standard

resource:
  master:
    requests:
      cpu: 2
      memory: 7.5Gi
  tserver:
    requests:
      cpu: 2
      memory: 7.5Gi

replicas:
  master: 3
  tserver: 3

partition:
  master: 3
  tserver: 3
```

If you want to change the defaults, you can use the command below. You can even do `helm install` instead of `helm upgrade` when you are installing on a Kubernetes cluster with configuration different than the defaults.

```sh
$ helm upgrade --set resource.tserver.requests.cpu=8,resource.tserver.requests.memory=15Gi yb-demo ./yugabyte
```

Replica count can be changed using the command below. Note only the tservers need to be scaled in a Replication Factor 3 cluster which keeps the masters count at 3.

```sh
$ helm upgrade --set replicas.tserver=5 yb-demo ./yugabyte
```

### LoadBalancer for Services

By default, the Yugabyte DB helm chart exposes only the master ui endpoint via LoadBalancer. If you wish to expose also the ycql and yedis services via LoadBalancer for your app to use, you could do that in couple of different ways.


If you want individual LoadBalancer endpoint for each of the services (YCQL, YEDIS), run the following command.

```sh
$ helm install yugabyte -f expose-all.yaml --namespace yb-demo --name yb-demo --wait
```

If you want to create a shared LoadBalancer endpoint for all the services (YCQL, YEDIS), run the following command.

```sh
$ helm install yugabyte -f expose-all-shared.yaml --namespace yb-demo --name yb-demo --wait
```

## Upgrade Cluster

You can perform rolling upgrades on the Yugabyte DB cluster with the following command. Change the `Image.tag` value to any valid tag from [Yugabyte DB's listing on the Docker Hub registry](https://hub.docker.com/r/yugabytedb/yugabyte/tags/). By default, the `latest` Docker image is used for the install.

```sh
$ helm upgrade yb-demo yugabyte --set Image.tag=1.1.0.3-b6 --wait
```

## Delete Cluster

Deleting the cluster involves purging the helm chart followed by deletion of the PVCs.

```sh
$ helm del --purge yb-demo
```

```sh
$ kubectl delete pvc --namespace yb-demo --all
```
