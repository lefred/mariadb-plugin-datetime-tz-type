# mariadb-plugin-datetime-tz-type


![mariabd-plugin-datetime-tz-type](logo/datetime_tz.png)

This is a MariaDB Server data type plugin that adds
`DATETIME_WITH_TIME_ZONE`.

The type stores values as a UTC instant and renders them in the current
session time zone. This differs from `DATETIME`, which stores and returns the
wall-clock value unchanged.

## Loading

The MTR suite loads the plugin with:

```sql
--plugin-load-add=$TYPE_DATETIME_TZ_SO
```

For a manually started server, load the built plugin shared object using the
normal MariaDB plugin loading mechanism for your build.

## Basic Example

```sql
MariaDB [test]> SET time_zone='+05:30';
Query OK, 0 rows affected (0.000 sec)

MariaDB [test]> CREATE TABLE ABC (ID INT, MyTime DATETIME);
Query OK, 0 rows affected (0.000 sec)

MariaDB [test]> CREATE TABLE XYZ (ID INT, MyTime DATETIME_WITH_TIME_ZONE);
Query OK, 0 rows affected (0.000 sec)

MariaDB [test]> INSERT INTO ABC VALUES (1, CURRENT_TIMESTAMP);
Query OK, 1 row affected (0.000 sec)

MariaDB [test]> INSERT INTO XYZ VALUES (1, CURRENT_TIMESTAMP);
Query OK, 1 row affected (0.000 sec)

MariaDB [test]> SELECT MyTime FROM ABC;
+---------------------+
| MyTime              |
+---------------------+
| 2026-07-03 22:31:26 |
+---------------------+
1 row in set (0.000 sec)

MariaDB [test]> SELECT MyTime FROM XYZ;
+---------------------------+
| MyTime                    |
+---------------------------+
| 2026-07-03 22:31:26+05:30 |
+---------------------------+
1 row in set (0.000 sec)
```

After changing the session time zone:

```sql
MariaDB [test]> SET time_zone='-05:00';
Query OK, 0 rows affected (0.001 sec)

MariaDB [test]> SELECT MyTime FROM ABC;
+---------------------+
| MyTime              |
+---------------------+
| 2026-07-03 22:31:26 |
+---------------------+
1 row in set (0.001 sec)

MariaDB [test]> SELECT MyTime FROM XYZ;
+---------------------------+
| MyTime                    |
+---------------------------+
| 2026-07-03 12:01:26-05:00 |
+---------------------------+
1 row in set (0.001 sec)
```

The `DATETIME` value is unchanged. The `DATETIME_WITH_TIME_ZONE` value is the
same instant rendered in the new session time zone.

## String Input

String values can include an explicit numeric offset:

```sql
MariaDB [test]> SET time_zone='+09:00';
Query OK, 0 rows affected (0.000 sec)

MariaDB [test]> INSERT INTO XYZ VALUES (2, '2026-07-03 08:12:20+01:00');
Query OK, 1 row affected (0.001 sec)

MariaDB [test]> INSERT INTO XYZ VALUES (3, '2026-07-03 08:12:20+01');
Query OK, 1 row affected (0.000 sec)

MariaDB [test]> INSERT INTO XYZ VALUES (4, '2026-07-03 08:12:20');
Query OK, 1 row affected (0.000 sec)

MariaDB [test]> SELECT ID, MyTime FROM XYZ ORDER BY ID;
+------+---------------------------+
| ID   | MyTime                    |
+------+---------------------------+
|    1 | 2026-07-04 02:01:26+09:00 |
|    2 | 2026-07-03 16:12:20+09:00 |
|    3 | 2026-07-03 16:12:20+09:00 |
|    4 | 2026-07-03 08:12:20+09:00 |
+------+---------------------------+
4 rows in set (0.001 sec)
```

Rows 2 and 3 specify an explicit `+01:00` source offset, so they are normalized
to the same UTC instant. Row 4 has no offset, so it is interpreted in the
current session time zone.

After changing the session time zone:

```sql
MariaDB [test]> SET time_zone='+02:00';
Query OK, 0 rows affected (0.001 sec)

MariaDB [test]> SELECT ID, MyTime FROM XYZ ORDER BY ID;
+------+---------------------------+
| ID   | MyTime                    |
+------+---------------------------+
|    1 | 2026-07-03 19:01:26+02:00 |
|    2 | 2026-07-03 09:12:20+02:00 |
|    3 | 2026-07-03 09:12:20+02:00 |
|    4 | 2026-07-03 01:12:20+02:00 |
+------+---------------------------+
4 rows in set (0.001 sec)
```

## Fractional Seconds

Fractional seconds use the normal temporal precision range `0..6`:

```sql
MariaDB [test]> SET time_zone='+05:30';
Query OK, 0 rows affected (0.000 sec)

MariaDB [test]> SET timestamp=1783063614.123456;
Query OK, 0 rows affected (0.000 sec)

MariaDB [test]> INSERT INTO t1 VALUES (CURRENT_TIMESTAMP(6));
Query OK, 1 row affected (0.000 sec)

MariaDB [test]> SELECT a FROM t1;
+----------------------------------+
| a                                |
+----------------------------------+
| 2026-07-03 12:56:54.123456+05:30 |
+----------------------------------+
1 row in set (0.000 sec)
```

## Limitations

The SQL type name is currently `DATETIME_WITH_TIME_ZONE`. Parser-native
multi-word syntax such as `DATETIME WITH TIME ZONE` is not implemented by this
plugin. This would require changes outside the plugin.

Only numeric offsets are parsed in string input:

```text
+HH
+HH:MM
-HH
-HH:MM
```

Named time zones in literal strings, such as `US/Eastern`, are not parsed.
