#!/usr/bin/env bash
# Tags: race, no-parallel

set -e

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

$CLICKHOUSE_CLIENT -q "DROP TABLE IF EXISTS mem"
$CLICKHOUSE_CLIENT -q "CREATE TABLE mem (x UInt64) engine = Memory"

function f {
  for _ in $(seq 1 300); do
    $CLICKHOUSE_CLIENT -q "SELECT count() FROM (SELECT * FROM mem SETTINGS max_threads=2) FORMAT Null;"
  done
}

function g {
  for _ in $(seq 1 100); do
    $CLICKHOUSE_CLIENT -n -q "
        INSERT INTO mem SELECT number FROM numbers(1000000);
        INSERT INTO mem SELECT number FROM numbers(1000000);
        INSERT INTO mem SELECT number FROM numbers(1000000);
        INSERT INTO mem VALUES (1);
        INSERT INTO mem VALUES (1);
        INSERT INTO mem VALUES (1);
        INSERT INTO mem VALUES (1);
        INSERT INTO mem VALUES (1);
        INSERT INTO mem VALUES (1);
        TRUNCATE TABLE mem;
    "
  done
}


spawn_with_timeout 30 f > /dev/null
spawn_with_timeout 30 g > /dev/null
wait

$CLICKHOUSE_CLIENT -q "DROP TABLE mem"
