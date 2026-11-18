#!/usr/bin/env python3
"""A stand-in for Schwab's Trader API, for tools/selftest.py: the token address
(Basic sign-in, an authorization code, a refresh token), the accounts and their
positions, transactions, quotes and price history, shaped as Schwab's are.
It is strict about what Schwab is strict about. Usage: mock_schwab.py PORTFILE LOG"""
import base64
import datetime
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

LOG = open(sys.argv[2], "a")
STATE = {"access": 0}
TODAY = datetime.date.today()

def day(n):
    return (TODAY - datetime.timedelta(days=n)).isoformat()

ACCOUNTS = [
    {"securitiesAccount": {
        "type": "MARGIN", "accountNumber": "12345678", "roundTrips": 0, "isDayTrader": False,
        "positions": [
            {"shortQuantity": 0, "averagePrice": 150.0, "longQuantity": 10, "marketValue": 1900.0,
             "instrument": {"assetType": "EQUITY", "cusip": "037833100", "symbol": "AAPL", "description": "APPLE INC"}},
            {"shortQuantity": 0, "averagePrice": 400.0, "longQuantity": 5.5, "marketValue": 2475.0,
             "instrument": {"assetType": "COLLECTIVE_INVESTMENT", "symbol": "VOO", "description": "VANGUARD S&P 500 ETF"}},
            {"shortQuantity": 0, "averagePrice": 1.0, "longQuantity": 250.0, "marketValue": 250.0,
             "instrument": {"assetType": "CASH_EQUIVALENT", "symbol": "SWVXX", "description": "SCHWAB VALUE ADVANTAGE MONEY FUND"}},
            {"shortQuantity": 0, "averagePrice": 2.5, "longQuantity": 1, "marketValue": 310.0,
             "instrument": {"assetType": "OPTION", "symbol": "AAPL  270115C00200000", "description": "AAPL 01/15/2027 200.00 C",
                            "putCall": "CALL", "underlyingSymbol": "AAPL"}},
        ],
        "initialBalances": {"cashBalance": 700.0, "liquidationValue": 5600.0},
        "currentBalances": {"cashBalance": 815.25, "liquidationValue": 5750.25, "availableFunds": 815.25}},
     "aggregatedBalance": {"currentLiquidationValue": 5750.25, "liquidationValue": 5750.25}},
    {"securitiesAccount": {
        "type": "CASH", "accountNumber": "87654321", "positions": [],
        "currentBalances": {"cashAvailableForTrading": 2000.0, "totalCash": 2000.0}}},
]

TRANSACTIONS = {
    "HASH1": [
        {"activityId": 9001, "time": day(40) + "T14:30:00+0000", "description": "Buy to Open", "accountNumber": "12345678",
         "type": "TRADE", "status": "VALID", "tradeDate": day(40) + "T14:30:00+0000", "netAmount": -1500.65,
         "transferItems": [
             {"instrument": {"assetType": "CURRENCY", "symbol": "CURRENCY_USD"}, "amount": 0.65, "cost": -0.65, "feeType": "COMMISSION"},
             {"instrument": {"assetType": "EQUITY", "symbol": "AAPL", "description": "APPLE INC"}, "amount": 10, "cost": -1500.0,
              "price": 150.0, "positionEffect": "OPENING"}]},
        {"activityId": 9002, "time": day(30) + "T15:00:00+0000", "description": "CASH DIVIDEND AAPL", "type": "DIVIDEND_OR_INTEREST",
         "status": "VALID", "tradeDate": day(30) + "T00:00:00+0000", "netAmount": 2.4,
         "transferItems": [{"instrument": {"assetType": "CURRENCY", "symbol": "CURRENCY_USD"}, "amount": 2.4, "cost": 0}]},
        {"activityId": 9003, "time": day(20) + "T15:00:00+0000", "description": "SCHWAB1 INT 10/28-11/26", "type": "DIVIDEND_OR_INTEREST",
         "status": "VALID", "tradeDate": day(20) + "T00:00:00+0000", "netAmount": 0.31, "transferItems": []},
        {"activityId": 9004, "time": day(15) + "T15:00:00+0000", "description": "ACH DEPOSIT", "type": "ACH_RECEIPT",
         "status": "VALID", "tradeDate": day(15) + "T00:00:00+0000", "netAmount": 2000.0,
         "transferItems": [{"instrument": {"assetType": "CURRENCY", "symbol": "CURRENCY_USD"}, "amount": 2000.0, "cost": 0}]},
        {"activityId": 9005, "time": day(10) + "T15:00:00+0000", "description": "Sell to Close", "type": "TRADE",
         "status": "VALID", "tradeDate": day(10) + "T15:00:00+0000", "netAmount": 439.9,
         "transferItems": [
             {"instrument": {"assetType": "CURRENCY", "symbol": "CURRENCY_USD"}, "amount": 0.1, "cost": -0.1, "feeType": "SEC_FEE"},
             {"instrument": {"assetType": "COLLECTIVE_INVESTMENT", "symbol": "VOO", "description": "VANGUARD S&P 500 ETF"},
              "amount": -1, "cost": 440.0, "price": 440.0, "positionEffect": "CLOSING"}]},
        {"activityId": 9006, "time": day(1) + "T15:00:00+0000", "description": "PENDING BUY", "type": "TRADE", "status": "PENDING",
         "netAmount": -100.0, "transferItems": []},
        {"activityId": 9007, "time": day(5) + "T15:00:00+0000", "description": "INTERNAL NOTE", "type": "MEMORANDUM", "status": "VALID",
         "netAmount": 0.0, "transferItems": []},
        {"activityId": 9008, "time": day(3) + "T15:00:00+0000", "description": "SHARES RECEIVED", "type": "RECEIVE_AND_DELIVER",
         "status": "VALID", "tradeDate": day(3) + "T00:00:00+0000", "netAmount": 0.0,
         "transferItems": [{"instrument": {"assetType": "EQUITY", "symbol": "BRK/B", "description": "BERKSHIRE HATHAWAY B"}, "amount": 2, "price": 0}]},
    ],
    "HASH2": [
        {"activityId": 9101, "time": day(8) + "T15:00:00+0000", "description": "WIRE IN", "type": "WIRE_IN", "status": "VALID",
         "tradeDate": day(8) + "T00:00:00+0000", "netAmount": 2000.0, "transferItems": []},
    ],
}

QUOTES = {
    "AAPL": {"assetMainType": "EQUITY", "symbol": "AAPL", "reference": {"description": "Apple Inc"},
             "quote": {"lastPrice": 191.5, "closePrice": 190.0, "netChange": 1.5, "netPercentChange": 0.79, "totalVolume": 1000}},
    "VOO": {"assetMainType": "EQUITY", "symbol": "VOO", "reference": {"description": "Vanguard S&P 500 ETF"},
            "quote": {"lastPrice": 451.0, "closePrice": 450.0, "netChange": 1.0, "netPercentChange": 0.22}},
    "SWVXX": {"assetMainType": "MUTUAL_FUND", "symbol": "SWVXX", "quote": {"nAV": 1.0, "closePrice": 1.0, "netChange": 0}},
}

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def answer(self, status, body):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def note(self, text):
        LOG.write(text + "\n")
        LOG.flush()

    def do_POST(self):
        url = urlparse(self.path)
        body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        form = {k: v[0] for k, v in parse_qs(body).items()}
        self.note("POST %s auth=%s type=%s body=%s" % (url.path, self.headers.get("Authorization"), self.headers.get("Content-Type"), body))
        if url.path != "/v1/oauth/token":
            return self.answer(404, {"message": "not found"})
        if self.headers.get("Authorization") != "Basic " + base64.b64encode(b"KEY:SECRET").decode():
            return self.answer(401, {"error": "invalid_client", "error_description": "Unauthorized"})
        if "client_id" in form or "client_secret" in form or "code_verifier" in form:
            return self.answer(400, {"error": "invalid_request", "error_description": "unexpected parameter"})
        if not self.headers.get("Content-Type", "").startswith("application/x-www-form-urlencoded"):
            return self.answer(400, {"error": "invalid_request", "error_description": "wrong content type"})
        ok = (form.get("grant_type") == "authorization_code" and form.get("code") == "C0.abc@"
              and form.get("redirect_uri") == "https://127.0.0.1") or (
              form.get("grant_type") == "refresh_token" and form.get("refresh_token") == "R1")
        if not ok:
            return self.answer(400, {"error": "invalid_grant", "error_description": "Bad authorization code or refresh token"})
        STATE["access"] += 1
        self.answer(200, {"expires_in": 1800, "token_type": "Bearer", "scope": "api", "refresh_token": "R1",
                          "access_token": "A%d" % STATE["access"], "id_token": "x"})

    def do_GET(self):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        self.note("GET %s %s auth=%s" % (url.path, json.dumps(q, sort_keys=True), self.headers.get("Authorization")))
        if self.headers.get("Authorization") != "Bearer A%d" % STATE["access"] or STATE["access"] == 0:
            return self.answer(401, {"errors": [{"id": "1", "status": "401", "title": "Unauthorized", "detail": "Client not authorized"}]})
        if url.path == "/trader/v1/accounts/accountNumbers":
            return self.answer(200, [{"accountNumber": "12345678", "hashValue": "HASH1"}, {"accountNumber": "87654321", "hashValue": "HASH2"}])
        if url.path == "/trader/v1/accounts":
            entries = json.loads(json.dumps(ACCOUNTS))
            if q.get("fields") != "positions":
                for e in entries:
                    e["securitiesAccount"].pop("positions", None)
            return self.answer(200, entries)
        m = re.match(r"^/trader/v1/accounts/(\w+)/transactions$", url.path)
        if m:
            stamp = r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$"
            if not re.match(stamp, q.get("startDate", "")) or not re.match(stamp, q.get("endDate", "")) or "types" not in q:
                return self.answer(400, {"message": "startDate, endDate (yyyy-MM-dd'T'HH:mm:ss.SSSZ) and types are required"})
            span = datetime.datetime.fromisoformat(q["endDate"][:19]) - datetime.datetime.fromisoformat(q["startDate"][:19])
            if span.days > 366:
                return self.answer(400, {"message": "the date range is limited to one year"})
            if m.group(1) not in TRANSACTIONS:
                return self.answer(400, {"message": "Invalid account number"})
            start = q["startDate"][:10]
            return self.answer(200, [t for t in TRANSACTIONS[m.group(1)] if t["time"][:10] >= start])
        if url.path == "/marketdata/v1/quotes":
            return self.answer(200, {s: QUOTES[s] for s in q.get("symbols", "").split(",") if s in QUOTES})
        if url.path == "/marketdata/v1/pricehistory":
            if q.get("symbol") != "AAPL":
                return self.answer(200, {"candles": [], "symbol": q.get("symbol"), "empty": True})
            candles, price = [], 150.0
            for n in range(400, -1, -1):
                d = TODAY - datetime.timedelta(days=n)
                if d.weekday() >= 5:
                    continue
                o = price
                price = round(price * (1 + ((n * 37) % 11 - 5) / 400.0), 2)
                ms = int(datetime.datetime(d.year, d.month, d.day, 6, 0, tzinfo=datetime.timezone.utc).timestamp() * 1000)
                candles.append({"open": o, "high": round(max(o, price) + 0.8, 2), "low": round(min(o, price) - 0.7, 2),
                                "close": price, "volume": 1000000 + n * 1000, "datetime": ms})
            return self.answer(200, {"candles": candles, "symbol": "AAPL", "empty": False})
        self.answer(404, {"message": "not found"})

server = HTTPServer(("127.0.0.1", 0), Handler)
open(sys.argv[1], "w").write(str(server.server_port))
server.serve_forever()
