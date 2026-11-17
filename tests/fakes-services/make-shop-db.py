#!/usr/bin/python3 -I
# Makes the Services windows' test database: two tables and a view.
#   make-shop-db.py FILE
import sqlite3
import sys

c = sqlite3.connect(sys.argv[1])
c.executescript("""
CREATE TABLE customers(id, name);
INSERT INTO customers VALUES (1, 'a'), (2, 'b'), (3, 'c');
CREATE TABLE orders(id);
INSERT INTO orders VALUES (1), (2);
CREATE VIEW recent AS SELECT * FROM orders;
""")
c.commit()
c.close()
