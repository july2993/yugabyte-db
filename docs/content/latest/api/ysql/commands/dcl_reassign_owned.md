---
title: REASSIGN OWNED
description: REASSIGN OWNED
summary: Roles (users and groups)
menu:
  latest:
    identifier: api-ysql-commands-reassign-owned
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dcl_reassign_owned
isTocNested: true
showAsideToc: true
---

## Synopsis

`REASSIGN OWNED` changes the ownership of database objects owned by any of the old_roles to new_role.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/reassign_owned,role_specification.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/reassign_owned,role_specification.diagram.md" /%}}
  </div>
</div>

## Semantics

`REASSIGN OWNED` is typically used to prepare for the removal of a role. It requires membership on both the source role(s) and target role.

## Examples

- Reassign all objects owned by john to yugabyte.

```sql
yugabyte=# reassign owned by john to yugabyte;
```

## See also

[`DROP OWNED`](../drop_owned)
[`CREATE ROLE`](../dcl_create_role)
[`GRANT`](../dcl_grant)
[`REVOKE`](../dcl_revoke)
[Other YSQL Statements](..)
