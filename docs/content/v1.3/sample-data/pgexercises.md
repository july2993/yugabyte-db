---
title: PgExercises sample database 
linkTitle: PgExercises
description: PgExercises sample database
image: /images/sample-data/pgexercises/pgsql.png
menu:
  v1.3:
    identifier: pgexercises
    parent: sample-data
    weight: 2753
isTocNested: true
showAsideToc: true
---

Download and install the PostgreSQL-compatible version of PgExercises on the Yugabyte DB distributed SQL database. Work through 81 exercises to learn SQL or test your knowledge.

## About the PgExercises sample database

The PgExercises sample database is based on the sample dataset used for the [PostgreSQL Exercises](https://pgexercises.com/) tutorial website. The dataset is for a new country club, with a set of members, facilities, and booking history.

The PostgreSQL Exercises website includes 81 exercises designed to be used as a companion to the official PostgreSQL documentation. The exercises on the PgExercises site range from simple SELECT statements and WHERE clauses, through JOINs and CASE statements, then on to aggregations, window functions, and recursive queries.

For further details about the data, see the [PosgresSQL Exercises' Getting Started page](https://pgexercises.com/gettingstarted.html).

The `exercises` database consists of three tables (for members, bookings, and facilities) and the table relationships as shown in the entity relationship diagram.

![PgExercises ER diagram](/images/sample-data/pgexercises/pgexercises-er-diagram.png)

## Install the PgExercises sample database

Follow the steps here to download and install the PgExercises sample database.

### Before you begin

To use the PgExercises sample database, you must have installed and configured Yugabyte DB. To get up and running quickly, see [Quick Start](/latest/quick-start/).

### 1. Download the SQL scripts

You can download the PGExercise SQL scripts that is compatible with Yugabyte DB from the [`sample` directory of the Yugabyte DB GitHub repository](https://github.com/yugabyte/yugabyte-db/tree/master/sample).

Here are the two files you’ll need.

- [`clubdata_ddl.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/clubdata_ddl.sql) — Creates the tables and other database objects
- [`clubdata_data.sql`](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/clubdata_data.sql) — Loads the sample data

### 2. Open the YSQL shell

To open the YSQL shell, run the `ysqlsh` command.

```sh
ysqlsh (11.2)
Type "help" for help.
postgres=#
```

### 3. Create the PgExercises database

To create the `exercises` database, run the following SQL `CREATE DATABASE` command.

```sql
CREATE DATABASE exercises;
```

Confirm that you have the `exercises` database by listing the databases on your cluster using the `\l` command.

```
postgres=# \l
```

Connect to the `exercises` database.

```
postgres=# \c exercises
You are now connected to database "exercises" as user "postgres".
exercises=#
```

### 4. Build the PgExercises tables and objects

To build the tables and database objects, run the `\i` command.

```
exercises=# \i /Users/yugabyte/clubdata_ddl.sql
```

You can verify that all three tables have been created by running the `\d` command.

```
exercises=# \d
```

### 5. Load the sample data

To load the `exercises` database with sample data, run the following command to execute commands in the file.

```
exercises=# \i /Users/yugabyte/clubdata_data.sql
```

You can verify that you have data to work with by running the following `SELECT` statement to pull data from the `bookings` table.

```
exercises=# SELECT * FROM bookings LIMIT 5;
```

## Explore the PgExercises database

You are now ready to start working through the [PostgreSQL Exercises](https://pgexercises.com/) exercises using Yugabyte DB as the backend. The 81 exercises at the PostgreSQL Exercises website are broken into the following major sections.

- [Simple SQL Queries](https://pgexercises.com/questions/basic/)
- [JOINs and Subqueries](https://pgexercises.com/questions/joins/)
- [Modifying Data](https://pgexercises.com/questions/updates/)
- [Aggregation](https://pgexercises.com/questions/aggregates/)
- [Working with Timestamps](https://pgexercises.com/questions/date/)
- [String Operations](https://pgexercises.com/questions/string/)
- [Recursive Queries](https://pgexercises.com/questions/recursive/)

Yugabyte DB returns the same results as expected based on the solutions on the PostgreSQL Exercises website, with the following exceptions.

- ["Work out the start times of bookings for tennis courts"](https://pgexercises.com/questions/joins/simplejoin2.html)
  - The `JOIN` does not return the correct row numbers. See [Yugabyte DB GitHub issue #1827](https://github.com/yugabyte/yugabyte-db/issues/1827).
- ["Find telephone numbers with parentheses"](https://pgexercises.com/questions/string/reg.html)
  - Yugabyte DB returns results with a sort order of strings different than in PostgreSQL due to [hash partitioning in Yugabyte DB](../architecture/docdb/sharding/#hash-partitioning-tables).
- ["Update a row based on the contents of another row"](https://pgexercises.com/questions/updates/updatecalculated.html)
  - Yugabyte DB returns an error because using the `FROM` clause in `UPDATE` is not yet supported. See [Yugabyte DB GitHub issue #738](https://github.com/yugabyte/yugabyte-db/issues/738).
- ["Delete based on a subquery"](https://pgexercises.com/questions/updates/deletewh2.html)
  - Yugabyte DB returns an error. See [Yugabyte DB GitHub issue #1828](https://github.com/yugabyte/yugabyte-db/issues/1828).
