# Charles Schwab

Money downloaded statements from the bank. Wayland Money downloads from
Charles Schwab, through Schwab's Trader API: your accounts, what each
holds, a year of transactions, and prices.

![Schwab Setup, after a download](images/schwab.png)

## Why not Project > APIs

The Studio has **Project > APIs...** for exactly this: describe a web
API and its sign-in once, and use its endpoints as tables. It can't sign
in to Schwab as it stands, for two reasons found while building this:

1. **Schwab only accepts an `https://` callback address** (the address
   the browser is sent back to after you sign in). The Studio's OAuth
   sign-in listens on `http://127.0.0.1` and has no setting for another.
2. **Schwab wants the app's key and secret in an `Authorization: Basic`
   header** when a code is exchanged for tokens. The Studio sends them in
   the body of the request.

So the program signs in with its own code. The Studio's code can do it:
`XMLHttpRequest` makes the requests, `Qt.btoa` builds the Basic header,
and `Qt.openUrlExternally` opens the browser. The one awkward step is the
callback: the browser is sent to `https://127.0.0.1`, where nothing is
listening, so it shows an error page, and you copy the address from its
address bar into the program. (This is how most small Schwab programs do
it.) [The last chapter](14-findings.md) lists both as things for the
Studio to gain.

## Your app at Schwab

Do this first; the approval takes a few days.

1. Make a developer account at
   [developer.schwab.com](https://developer.schwab.com) (it's separate
   from your brokerage sign-in).
2. **Dashboard > Create App**. Add both API products: **Accounts and
   Trading Production** and **Market Data Production**.
3. **Callback URL**: `https://127.0.0.1` exactly, with no `/` at the end.
4. The app starts as *Approved - Pending*. Wait until it says **Ready
   For Use**.
5. Open the app for its **App Key** and **Secret**.

Wayland Money only ever reads. It never places an order.

## The library's part

Add to the end of HomeForm's **(General)**:

{{code HomeForm (General) schwab}}

{{code HomeForm (General) schwab-start}}

How it's built:

- **Nothing here waits.** `XMLHttpRequest` sends its request and the
  program carries on; the answer comes back later to a function. So
  every one of these takes a function `done` to call with the result,
  and a download is a chain of them.
- **Two tokens.** Schwab gives an *access token* that lasts 30 minutes
  and goes on every request, and a *refresh token* that lasts 7 days and
  gets new access tokens. `schwabToken` hands back the access token it
  has, or quietly gets a new one first. When the refresh token itself
  has run out, it says a new sign-in is needed: Schwab's rule, every
  seven days.
- **`schwabGet(path, done)`** is all any other code needs.
- **`updateQuotes`** asks for many symbols in one request and writes
  each price to its security.

## SchwabForm

**Project > Add Form**:

{{form SchwabForm}}

{{elements SchwabForm *}}

`secretBox` has **Password** True: dots for what's typed.

**(General)**:

{{code SchwabForm (General)}}

{{events SchwabForm *}}

### What a download does

`syncAll()` makes five kinds of request, in order:

| Request | What comes back | What's done with it |
|---|---|---|
| `GET /trader/v1/accounts/accountNumbers` | Each account's number and a *hash* of it | Matched to Accounts by **Account Number**; a new one is made as *Schwab ...1234*. The hash is kept in **Schwab Link**: Schwab wants it, not the number, in every other address. |
| `GET /trader/v1/accounts?fields=positions` | Each account's balances and positions | The account's Holdings are replaced with Schwab's positions; each security gets a price. |
| `GET /trader/v1/accounts/{hash}/transactions` | The transactions between two dates | Each new one becomes a Transaction, marked Cleared, with Schwab's id. |
| `GET /marketdata/v1/quotes` | The latest price of each symbol | Securities' Last Price and Change; every holding revalued. |
| `GET /marketdata/v1/pricehistory` | Daily bars for one symbol | (From SecuritiesForm's **Download Prices**:) the Prices table. |

The details that matter:

- **Transactions** are asked for from a week before the last download
  (the first time, 364 days: Schwab allows a year at a time). The dates
  go as `2026-11-18T08:00:00.000Z`, which is what a JavaScript date's
  `toISOString()` gives. The overlap doesn't make duplicates, because
  **Schwab Id** is No Duplicates and `takeTransaction` looks first.
- **What kind of transaction** comes from Schwab's `type`: a `TRADE` is
  a Buy or a Sell with its security, shares, price and fees; a
  `DIVIDEND_OR_INTEREST` gets its income category; deposits, withdrawals
  and wires are plain transactions for you to categorize. Ones still
  `PENDING` are left for next time.
- **The cash is made to agree.** Schwab says what the account is worth;
  less what it holds, that's its cash. The download can't see
  transactions older than a year, so it sets the account's **Opening
  Balance** to whatever makes the register's balance come out at that
  cash. The register's last line always matches Schwab.

## Into the Home page

`syncButton`, **clicked** (the menu and the tool bar already call the
same thing, in the code from chapter 5):

{{code HomeForm syncButton.clicked}}

A new menu **&Online** after Investing:

{{menu HomeForm onlineSync,onlineSetup}}

{{toolbar HomeForm syncTool}}

## The first sign-in

1. **Online > Schwab Setup...**. Paste the **App Key** and **Secret**;
   the Callback URL is already `https://127.0.0.1`.
2. **1. Sign In at Schwab...**. The browser opens Schwab's own sign-in
   page. Sign in with your brokerage name and password, tick the accounts
   to share, and **Allow**.
3. The browser is sent to `https://127.0.0.1/?code=...` and says it can't
   connect. That's expected. **Copy the whole address** from the address
   bar.
4. Back in Schwab Setup, paste it and **Finish Sign-In**. Be quick: the
   code in that address only works for about half a minute.
5. The log says *Signed in.*, and the first download starts by itself:
   your accounts appear, then their transactions, then prices.

![A Schwab account's register after a download](images/register-schwab.png)

Then: rename the new accounts in **Accounts > Account List** (and set a
Retirement account's Type). From now on **F9** downloads. Tick
**Download when Wayland Money starts** to have it happen by itself.

After seven days a download says *Schwab wants a new sign-in*: steps 2 to
4 again. The line under the button says when that will be.

## Keeping it safe

- The App Key, the Secret and the tokens are kept in the **Settings**
  table, in the program's database file, as plain text. Anyone who can
  read that file can read your Schwab accounts until the refresh token
  runs out. Keep the file to yourself; don't send `money.db` or
  `data.db` to anyone, and don't put it in a shared folder.
- **Sign Out** forgets the tokens at once.
- The program asks Schwab only to read. Even so, treat the Secret as a
  password: if it gets out, make a new one at developer.schwab.com.

## When something isn't right

| The log says | What it means |
|---|---|
| *Unauthorized*, at Finish Sign-In | The App Key or Secret is wrong, or the app isn't **Ready For Use** yet. |
| *Bad authorization code...* or similar | The pasted code was too old (over half a minute), or it was used already. Sign in again. |
| *Schwab wants a new sign-in* | The seven days are up. |
| *No answer from Schwab* | No internet, or Schwab is down. |
| *Schwab said 400* on transactions | Schwab didn't like the dates or the account. Sign out and in again so the account hashes are fresh. |
| *Skipped ...: Securities won't take that symbol* | A symbol with a character the Securities table's Pattern doesn't allow. Widen the Pattern. |

## What was checked, and what wasn't

This chapter's code was run against a stand-in for Schwab
(`tools/mock_schwab.py`) that is strict about the things Schwab is strict
about: the Basic header, the form fields, the hash in the address, the
date format, the one-year limit. The requests it received are what
Schwab's API is documented to want. From the program, a real request to
`api.schwabapi.com` without a token got Schwab's own *401 Unauthorized*
back, so the connection itself works from a Studio program.

It was **not** run against a real Schwab account: that needs your app
and your sign-in. The names of the fields in Schwab's answers come from
Schwab's published API as other programs use it. If a download leaves
something blank, these are the places to look, in `SchwabForm` and the
library:

- an account's worth: `aggregatedBalance.liquidationValue`, then
  `currentBalances.liquidationValue`, `initialBalances.liquidationValue`;
- a position: `longQuantity`, `shortQuantity`, `averagePrice`,
  `marketValue`, `instrument.symbol`, `instrument.assetType`;
- a transaction: `activityId`, `type`, `status`, `tradeDate`,
  `netAmount`, `description`, `transferItems`;
- a quote: `quote.lastPrice` (then `mark`, `nAV`, `closePrice`),
  `quote.netChange`, `quote.netPercentChange`;
- price history: `candles`, each with `open`, `high`, `low`, `close`,
  `volume` and `datetime` (milliseconds).

To see exactly what Schwab sent, put `console.log(JSON.stringify(data))`
in `schwabGet` and run from the Studio: it prints in the Output window.
