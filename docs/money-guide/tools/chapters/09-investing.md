# Investments

An investment account has cash, like any account, and holdings: so many
shares of a security, worth today's price. This chapter builds the
securities and their price charts, a form to record a trade, and the
portfolio. Everything works by hand first; [chapter 10](10-schwab.md)
then fills the same tables from Schwab.

![The portfolio](images/portfolio.png)

## The library's part

In HomeForm's **(General)**, **delete** the stand-in `rebuildHoldings`
from chapter 5 and add at the end:

{{code HomeForm (General) investing}}

- **Holdings are worked out, not typed.** `rebuildHoldings` walks an
  account's trades in date order and keeps, for each security, the
  shares held and what they cost (average cost: selling a quarter of the
  shares takes a quarter of the cost with it).
- **`revalue()`** sets each holding's market value from its security's
  last price. An option's price is for one share and a contract is a
  hundred.
- An account's value on the Home page is its cash plus its holdings:
  `balances()` has been adding Holdings in since chapter 3.

## SecuritiesForm

Open the **Securities** table and **Project > Add Form for Securities**.
The top half is as the Studio draws it: the security's fields, New, Save
and Delete, and a grid of the securities. Under it goes a price chart.

![A security and its prices](images/securities.png)

1. The form: **Height** `612`.
2. `grid`: **Columns** `Symbol`, `Company`, `LastPrice`.
3. `priceTimePicker`: **Enabled** False.
4. Add:

{{elements SecuritiesForm rangeLabel,rangeCombo,averageCheck,bandsCheck,volumeCheck,rsiCheck,liveCheck,chart,downloadButton,statusLabel}}

`chart` is a stock graph:

{{graph SecuritiesForm chart}}

**(General)**:

{{code SecuritiesForm (General)}}

{{events SecuritiesForm *}}

- **`showChart()`** gives the graph its bars from the Prices table:
  `chart.setData(labels, [{ open, high, low, close, volume }])`.
- **The check boxes** set `chart.overlays` (lines over the prices: a
  20-day average, Bollinger bands) and `chart.studies` (panes under
  them: volume, RSI). The Studio's graph works these out itself.
- **A price typed by hand** (Last Price, then Save) becomes today's bar,
  in the form's **saved** event, and every holding is revalued.
- **Download Prices** and **Live price** ask Schwab, through functions
  the next chapter adds to the library. Until then they'll say so in the
  Output window and do nothing.
- In the running program the wheel zooms the chart, dragging goes back
  in time, and resting the pointer on a bar shows its figures.

## TradeForm

![Recording a trade](images/trade.png)

**Project > Add Form**:

{{form TradeForm}}

It's a second form on the Transactions table: a trade is a transaction
with an Action.

{{elements TradeForm *}}

**(General)**:

{{code TradeForm (General)}}

{{events TradeForm *}}

The form shows only the boxes the chosen action needs: shares and price
for a buy or a sell, a plain amount for a dividend, interest or a fee.
`cashFor()` is what the trade does to the account's cash; the line at
the bottom says it in words before you press Enter.

**Account** lists every account, because a Lookup's list can't be
narrowed; Enter checks that the one chosen is an investment account.

## PortfolioForm

**Project > Add Form**:

{{form PortfolioForm}}

{{elements PortfolioForm *}}

{{graph PortfolioForm graph1}}

**(General)**:

{{code PortfolioForm (General)}}

{{events PortfolioForm *}}

## Into the menu

A new menu **&Investing** after Bills:

{{menu HomeForm investPortfolio,investTrade,investSecurities}}

*Record a Trade* has no Action: `menu1.itemClicked` opens TradeForm for
the selected account.

{{toolbar HomeForm portfolioTool}}

## Try it

1. In the Brokerage register, a Deposit of `1000`.
2. **Investing > Securities & Prices...**: New, Symbol `ACME`, Company
   `Acme Corp`, Save. Try a symbol of `ab c!`: *A symbol is capitals and
   digits.*
3. Select Brokerage on the Home page, **Investing > Record a Trade...**:
   Buy, ACME, Shares `10`, Price each `20`, Commission `4.95`. The line
   says *204.95 leaves the account's cash.* Enter, Close.
4. **Investing > Portfolio...**: ACME, 10 shares, cost 204.95, no price
   yet.
5. Back in Securities, set ACME's **Last Price** to `26.5` and Save. The
   chart gets its first bar; the portfolio shows a value of 265.00 and a
   gain; the Home page's Brokerage is cash plus holdings.
6. The Brokerage register shows the buy, with its balance in cash and
   **Investments** beside it.
