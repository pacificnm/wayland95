# Trading Lab content

Static content converted once from the Electron/TypeScript Trading Lab, **version 1.1.1, commit b5f5059**,
and compiled into the Qt app as resources. Do not edit these files by hand: change the converter below and regenerate.

| File | What it holds | Upstream source |
|---|---|---|
| `strategies.json` | The 44 built-in strategy documents (`StrategyDoc`: id, title, category, level, minutes, summary, tags, body, quiz, chartSetup, source). Bodies are markdown with the disclaimer footer from `doc()` already appended; ` ```chartdiagram ` blocks hold the diagram JSON to draw. | `renderer/src/data/strategies.ts` (+ `strategiesDay/Long/Process/Patterns.ts`, `strategyDoc.ts`) |
| `strategy-meta.json` | Categories, levels, progress states and labels, and the editor's new-document defaults. | `shared/strategies.ts`, `views/StrategiesView.tsx` |
| `patterns.json` | The 66 candlestick patterns' teaching entries (name, bias, group, rarity, size, context, summary, rules, story, confirm, caution, drawn `example` bars as [open, high, low, close] on 0-100), `patternIds` order, rarity tiers and notes, groups. Detection is C++ (`src/core/candlepatterns.cpp`). | `shared/candlePatterns.ts` |
| `tools.json` | Claude tool definitions (`name`, `label`, `description`, `input_schema`). `label` is UI only: the API gets `name`, `description`, `input_schema` (main/chat.ts strips the rest). | `shared/tools.ts` |
| `chat.json` | System prompt, model list, effort levels, default model/effort. | `shared/chat.ts` |
| `help/toc.json`, `help/pages/*.md` | Help library table of contents and pages (verbatim). The screenshots in upstream `help/img` show the Electron UI and are not copied. | `renderer/src/help/toc.ts`, `renderer/src/help/pages/` |
| `chart.json` | Intervals (bars per day), ranges, min/max bar count for a usable range/interval pair, chart types, scale modes, default chart settings, colours for duplicate studies. | `renderer/src/chart/timeframe.ts`, `settings.ts`, `colors.ts` |
| `studies.json` | Study (indicator) metadata: id, name, category, pane, parameters with defaults, reference levels, cloud fill, and the outputs each draws with default colours. The maths is C++. | `renderer/src/chart/studies.ts` (+ `colors.ts` `studyOutputs`) |
| `screens.json` | Screener default filters and presets, market movers default filter, congress trades default filter. | `renderer/src/data/screener.ts`, `market.ts`, `congress.ts` |
| `fmp-caps.json` | FMP plan capabilities and which screens, tools and intervals depend on each. | `shared/fmpCaps.ts` |
| `lists.json` | Brokers, account type labels, margin multiple, journal emotions, order side/type labels, portfolio holding kinds, default risk rules. | `shared/accounts.ts`, `journal.ts`, `trade.ts`, `portfolio.ts`, `position.ts` |

## Regenerating

With the upstream repository checked out and `npm install` run in its `app/` folder (Node 20), save the two files
below in an empty folder and run:

```bash
node build.mjs /path/to/trading-labs.app/app          # bundles entry.ts with the app's esbuild into convert.cjs
node convert.cjs /path/to/trading-labs.app/app/src /path/to/wayland95/apps/tradinglab/content
```

`build.mjs`:

```js
// node build.mjs <upstream app dir>  -> convert.cjs next to this file
import path from 'path'
import { createRequire } from 'module'
const app = path.resolve(process.argv[2])
const { build } = createRequire(path.join(app, 'package.json'))('esbuild')
const here = path.dirname(new URL(import.meta.url).pathname)
await build({
  entryPoints: [path.join(here, 'entry.ts')], bundle: true, platform: 'node', format: 'cjs', outfile: path.join(here, 'convert.cjs'),
  alias: { '@src': path.join(app, 'src') },
  plugins: [{
    // help/toc.ts uses Vite's import.meta.glob for page bodies and images; the converter copies pages itself.
    name: 'no-glob', setup(b) {
      b.onLoad({ filter: /help[\\/]toc\.ts$/ }, async (a) => {
        const fs = await import('fs')
        return { contents: fs.readFileSync(a.path, 'utf8').replace(/import\.meta\.glob\([^)]*\)/g, '({})'), loader: 'ts' }
      })
    }
  }]
})
```

`entry.ts`:

```ts
// Converts Trading Lab's static content (TypeScript modules) into JSON for the Qt port.
// Usage: node convert.cjs <upstream app/src> <output content dir>
import * as fs from 'fs'
import * as path from 'path'
import { BUILTIN_STRATEGIES } from '@src/renderer/src/data/strategies'
import { CATEGORIES, PROGRESS_LABEL } from '@src/shared/strategies'
import { CANDLE_PATTERNS, RARITY_NOTE, PATTERN_IDS } from '@src/shared/candlePatterns'
import { TOOLS } from '@src/shared/tools'
import { SYSTEM_PROMPT, MODELS, EFFORTS, DEFAULT_PREFS } from '@src/shared/chat'
import { HELP } from '@src/renderer/src/help/toc'
import { INTERVALS, RANGES } from '@src/renderer/src/chart/timeframe'
import { DEFAULT_CHART_SETTINGS } from '@src/renderer/src/chart/settings'
import { DUP_PALETTE, studyOutputs } from '@src/renderer/src/chart/colors'
import { STUDIES } from '@src/renderer/src/chart/studies'
import { DEFAULT_FILTERS, PRESETS } from '@src/renderer/src/data/screener'
import { DEFAULT_MOVER_FILTER } from '@src/renderer/src/data/market'
import { DEFAULT_TRADE_FILTER } from '@src/renderer/src/data/congress'
import { CAPS, SCREEN_CAPS, TOOL_CAPS, INTERVAL_CAP } from '@src/shared/fmpCaps'
import { BROKERS, ACCOUNT_TYPE_LABEL, MARGIN_MULTIPLE } from '@src/shared/accounts'
import { EMOTIONS } from '@src/shared/journal'
import { SIDE_LABEL, TYPE_LABEL } from '@src/shared/trade'
import { KINDS } from '@src/shared/portfolio'
import { DEFAULT_RULES } from '@src/shared/position'

const [srcDir, outDir] = process.argv.slice(2)
if (!srcDir || !outDir) throw new Error('usage: node convert.cjs <upstream app/src> <content dir>')
const write = (name: string, data: unknown) => {
  const file = path.join(outDir, name)
  fs.mkdirSync(path.dirname(file), { recursive: true })
  fs.writeFileSync(file, JSON.stringify(data, null, 2) + '\n')
  console.log('wrote', file)
}

write('strategies.json', BUILTIN_STRATEGIES.map((s) => ({
  id: s.id, title: s.title, category: s.category, level: s.level, minutes: s.minutes, summary: s.summary,
  tags: s.tags, body: s.body, quiz: s.quiz, chartSetup: s.chartSetup, source: s.source
})))

write('strategy-meta.json', {
  categories: CATEGORIES,
  // StrategiesView.tsx LEVELS / STATES (type-only in shared/strategies.ts)
  levels: ['beginner', 'intermediate', 'advanced'],
  progressStates: ['new', 'learning', 'practiced', 'confident'],
  progressLabel: PROGRESS_LABEL,
  // StrategiesView.tsx editor defaults for a new document
  newDoc: { category: 'My strategies', level: 'beginner', minutes: 10, body: '## The idea\n\n## Rules\n- Entry:\n- Stop:\n- Target:\n\n## Common mistakes\n\n## Practice\n' }
})

write('patterns.json', {
  patternIds: PATTERN_IDS,
  rarityTiers: ['common', 'uncommon', 'rare', 'very rare'],
  rarityNote: RARITY_NOTE,
  groups: ['Single candle', 'Two candles', 'Three candles', 'Four or five candles'],
  patterns: CANDLE_PATTERNS.map((p) => ({
    id: p.id, name: p.name, bias: p.bias, group: p.group, rarity: p.rarity, size: p.size, context: p.context,
    summary: p.summary, rules: p.rules, story: p.story, confirm: p.confirm, caution: p.caution, example: p.example
  }))
})

write('tools.json', TOOLS)

write('chat.json', { systemPrompt: SYSTEM_PROMPT, models: MODELS, efforts: EFFORTS, defaultPrefs: DEFAULT_PREFS })

write('help/toc.json', HELP)
const pagesIn = path.join(srcDir, 'renderer/src/help/pages')
for (const f of fs.readdirSync(pagesIn).filter((f) => f.endsWith('.md')).sort()) {
  fs.mkdirSync(path.join(outDir, 'help/pages'), { recursive: true })
  fs.copyFileSync(path.join(pagesIn, f), path.join(outDir, 'help/pages', f))
}

write('chart.json', {
  intervals: INTERVALS,
  ranges: RANGES,
  // timeframe.ts MIN_BARS / MAX_BARS (not exported): isAllowed needs minBars <= barCount <= maxBars
  minBars: 4,
  maxBars: 10000,
  chartTypes: ['candles', 'hollow', 'bars', 'line', 'area', 'heikin'],
  scaleModes: ['normal', 'log', 'percent'],
  defaultSettings: DEFAULT_CHART_SETTINGS,
  dupPalette: DUP_PALETTE
})

write('studies.json', STUDIES.map((s) => ({
  id: s.id, name: s.name, category: s.category, pane: s.pane, params: s.params,
  ...(s.levels ? { levels: s.levels } : {}), ...(s.cloud ? { cloud: s.cloud } : {}),
  outputs: studyOutputs(s).map((o) => ({ name: o.name, kind: o.kind, color: o.color,
    ...(o.tones ? { tones: o.tones } : {}), ...(o.shift ? { shift: o.shift } : {}), ...(o.steps ? { steps: o.steps } : {}) }))
})))

write('screens.json', {
  screener: { defaultFilters: DEFAULT_FILTERS, presets: PRESETS },
  market: { defaultMoverFilter: DEFAULT_MOVER_FILTER },
  congress: { defaultTradeFilter: DEFAULT_TRADE_FILTER }
})

write('fmp-caps.json', { caps: CAPS, screenCaps: SCREEN_CAPS, toolCaps: TOOL_CAPS, intervalCap: INTERVAL_CAP })

write('lists.json', {
  brokers: BROKERS, accountTypeLabel: ACCOUNT_TYPE_LABEL, marginMultiple: MARGIN_MULTIPLE,
  emotions: EMOTIONS, sideLabel: SIDE_LABEL, orderTypeLabel: TYPE_LABEL, portfolioKinds: KINDS, defaultRiskRules: DEFAULT_RULES
})
```
