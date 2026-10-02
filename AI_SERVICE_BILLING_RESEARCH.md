# LagDaemon Virtual Engineer Billing Research

Date: 2026-09-23

## Mission

Add a paid LagDaemon Virtual Engineer option beside BYOK in LagDaemon apps. Users can either:

1. Bring their own virtual engineer by configuring their own model/vendor/API key.
2. Hire a LagDaemon Virtual Engineer, funded by prepaid account balance.

The important distinction: LagDaemon is not selling access to an AI model. LagDaemon is selling engineering capability, workflow, domain expertise, personas, tools, LiteSemRAG+ISD context, validation procedures, and managed execution. LLM calls are backend implementation details. The user submits engineering requests; the LagDaemon backend decides whether to answer from local knowledge, use deterministic tools, run analysis, preprocess the request, call one or more LLM vendors, or chain those steps.

This should be managed from LagDaemon.com and reused by FrustIDE, Station, DjehutiSuite tools, and later Virtual Engineer/agent systems.

## Executive Recommendation

Build this as a prepaid Virtual Engineer wallet, not as an invoice-later system and not as an OpenAI-token resale system.

The safe first product shape is:

- User prepays $10/$25/$50/etc. into LagDaemon Engineer Credits.
- App requests go through LagDaemon's Virtual Engineer Gateway.
- Gateway routes the request through the appropriate persona, policy, context, tools, and model/vendor backend.
- Gateway records exact backend cost from provider responses and tool usage.
- Gateway prices the delivered engineering work using a versioned internal cost catalog plus service margin.
- Gateway deducts from the user's wallet ledger.
- A nightly reconciliation job compares internal calculated cost against OpenAI Organization Costs API totals.
- The gateway refuses new work when user balance falls below a configurable reserve.

Do not expose raw OpenAI API compatibility as a public proxy. Position this as "Hire the LagDaemon Virtual Engineer." OpenAI's business agreement permits API integration into customer applications and making those applications available to end users, but it also prohibits reselling or leasing account access and transferring API keys. That means the business model should be framed as engineering service functionality, not "buy OpenAI tokens from us."

## Product Positioning

The user-facing offer should be:

```text
Bring your own engineer, or hire ours.
```

BYOK mode means the user controls provider/model/API key choices. LagDaemon supplies the app UI and possibly local context/tooling, but the user's provider account carries model cost and provider policy.

LagDaemon Engineer mode means the user buys access to LagDaemon's engineered assistant:

- Frust language expertise.
- Station/audio/DSP expertise.
- DjehutiSuite workflow expertise.
- Persona/focus-area selection.
- LiteSemRAG+ISD context retrieval.
- Process and policy cards.
- Tool orchestration.
- Validation procedures.
- Code/document/project generation.
- Debugging and repair loops.
- Usage/account/budget controls.

In LagDaemon Engineer mode the user should not choose OpenAI, Anthropic, Gemini, model names, context windows, or sampling parameters. The request is routed by LagDaemon. The backend may use OpenAI first, but the product should be designed so later it can use multiple vendors or local models without changing the user contract.

The hosted service is therefore not:

- "OpenAI access."
- "A cheaper API key."
- "Choose a model and chat."
- "Token resale."

It is:

- "Pay-as-you-go specialist engineering assistance inside LagDaemon tools."
- "Put money on account and the engineer comes alive."
- "Request work; the backend decides how to fulfill it."

## Current Source Findings

### OpenAI

OpenAI publishes model prices per 1M tokens on the official API pricing page. The page includes separate rates for input, cached input, cache writes, and output, with different rates for standard/batch/flex/fast and short/long context in newer model families.

Important current examples from the official pricing page:

- `gpt-5.6-sol` cyber model listed at $4.00 input, $0.40 cached input, $5.00 cache writes, and $20.00 output per 1M short-context tokens.
- `gpt-6-luna` listed under all models at $0.05 input, $0.005 cached input, $0.0625 cache writes, and $0.25 output per 1M short-context tokens in one processing table.
- OpenAI notes that tokens are billed at the chosen model's input/output rates and that built-in tools can add additional billing, including web search/content tokens and container sessions.

OpenAI API responses include a `usage` object with input tokens, output tokens, total tokens, and details. The Organization Usage API can aggregate usage by fields such as model, project, user, API key, batch, and service tier. OpenAI also has an Organization Costs endpoint that returns monetary cost buckets, but OpenAI says usage and costs may not reconcile perfectly and recommends the Costs endpoint/dashboard for financial reconciliation.

OpenAI has project/org spend limits. These should be set as a hard guardrail on LagDaemon's OpenAI account so one bug or abusive user cannot drain the bank account.

OpenAI Services Agreement notes:

- You can integrate OpenAI APIs into Customer Applications and make those applications available to End Users.
- You are responsible for end-user activity through the application.
- You may not resell/lease account access, share account credentials, transfer API keys, or circumvent rate/usage limits.

### Chase / J.P. Morgan Payments

Chase Payment Solutions supports online payments, payment gateway use, invoicing, recurring billing, and major cards/wallets. Public Chase fee examples include:

- 2.9% + $0.25 for online orders.
- 3.5% + $0.15 for keyed/payment-link style transactions.

J.P. Morgan's newer Commerce / Online Payments API supports:

- Card, wallet, local, and alternative payment methods.
- Stored consumer profiles.
- Recurring payments.
- Tokenization.
- Webhook notifications.
- Checkout API using Drop-in UI or Hosted Payments Page.
- Cardholder-initiated transactions (CIT) and merchant-initiated transactions (MIT) for stored-card usage.

J.P. Morgan's developer docs say a live account requires a developer account, workspace project, client profile, product selection/configuration, test credentials, integration testing, client contract, and production credentials.

For our use case, the clean first implementation is Chase/J.P. Morgan Hosted Payments Page or Drop-in UI to let users add prepaid credits. That keeps raw card data out of LagDaemon systems and avoids us storing PAN data directly.

## Product Model

### User Choices In App

Every AI-enabled app should expose:

- Engineer mode: `Bring Your Own Engineer` or `Hire LagDaemon Engineer`.
- If BYOK/BYOE: user configures their own provider, model, API key, and limits. Store the key locally or in the user's configured secure vault, never on LagDaemon unless explicitly requested.
- If LagDaemon Engineer: user signs into LagDaemon account; app calls LagDaemon Virtual Engineer Gateway.
- Display approximate available balance and estimated cost controls.
- Display the persona/focus area, not the backend vendor/model.

Possible LagDaemon Engineer personas:

- Frust Language Engineer
- Station Recording Engineer
- DSP Device Engineer
- DjehutiSuite Workflow Engineer
- Documentation/Help Engineer
- Build/Release Engineer
- Research Engineer

Each persona should map to its own context bundles, tool permissions, validation procedures, and backend routing policy.

### Account Funding

Use prepaid engineer-credit top-ups:

- Minimum top-up: maybe $5 or $10.
- Suggested tiers: $10, $25, $50, $100.
- Auto-refill optional: user chooses threshold and refill amount.
- Account reserve: gateway rejects work if estimated maximum cost exceeds balance minus reserve.

Why prepaid:

- OpenAI charges LagDaemon whether or not the user later pays.
- Other future providers may also charge LagDaemon immediately.
- Credit card authorization/capture for every tiny model call is uneconomic.
- A prepaid ledger gives exact audit trail and fewer payment events.

### Margin

Internal cost formula:

```text
provider_cost = sum(provider token/tool/audio/image/video/session units * provider unit price)
tool_cost = internal compute/storage/transcription/rendering/search cost
base_cost = provider_cost + tool_cost
service_fee = max(minimum_request_fee, base_cost * margin_multiplier)
user_charge = base_cost + service_fee
```

Recommended first margin:

- 30% markup for normal users.
- Minimum per-call fee, e.g. $0.0001 or $0.001, to cover ledger/payment overhead.
- Extra reserve for agentic tools, because retries and tool loops can balloon.

Example:

```text
Provider cost:    $0.0312
Tool cost:        $0.0020
Base cost:        $0.0332
Margin 30%:       $0.00996
User debit:       $0.04316
Ledger rounded:   $0.0432
```

Never round individual provider cost down. Keep internal ledger in microdollars or integer millionths of a dollar.

## Required Architecture

### 1. Virtual Engineer Gateway

A server-side service on LagDaemon.com:

- Accepts app request from authenticated user/device.
- Checks account balance and policy.
- Classifies request intent, risk, and required persona.
- Runs preprocessing and context selection.
- Applies task budget, tool policy, and backend routing policy.
- Decides whether the request can be handled locally or requires model/vendor calls.
- Calls selected backend provider(s) using LagDaemon-owned credentials.
- Receives provider/tool responses.
- Extracts exact usage/cost basis from provider responses.
- Calculates charge using current pricing/catalog rules.
- Records immutable usage ledger event.
- Deducts wallet balance.
- Returns response to app.

The apps never receive the LagDaemon OpenAI API key or future vendor keys. In LagDaemon Engineer mode the apps also should not expose vendor/model selection. They send requests to an engineering service; the service chooses the fulfillment path.

### 2. Router And Persona Layer

The Virtual Engineer Gateway should have a routing layer before any LLM call:

```text
user request
  -> authenticate/account/budget
  -> intent classifier
  -> persona selector
  -> context planner
  -> tool/model route planner
  -> execution loop
  -> verification
  -> billing finalization
  -> response
```

Routing decisions should consider:

- app: FrustIDE, Station, website, DjehutiSuite
- persona/focus area
- user request type: explain, plan, edit, build, debug, research, generate DSP device
- risk level
- expected cost
- required tools
- need for source-code access
- whether local LiteSemRAG+ISD is sufficient
- whether a small cheap model can classify/plan
- whether a stronger model is needed for final engineering work
- whether a specialist future vendor/model is better

This layer is where LagDaemon expertise lives. It lets weaker/cheaper models perform narrow routing and retrieval tasks, while stronger models are reserved for difficult engineering work.

### 3. Pricing Catalog

Create a versioned internal catalog:

```json
{
  "provider": "openai",
  "source_url": "https://developers.openai.com/api/docs/pricing",
  "effective_at": "2026-09-23T00:00:00Z",
  "model": "gpt-5.6-sol",
  "processing_mode": "standard",
  "context_class": "short",
  "currency": "usd",
  "unit": "per_1m_tokens",
  "input": 4.00,
  "cached_input": 0.40,
  "cache_write": 5.00,
  "output": 20.00
}
```

The scraper should fetch the official OpenAI pricing page, parse model tables, produce a new catalog version, and require human approval before activating if prices changed materially. Do not silently change billing rates in production without a review record.

Pricing must support:

- input tokens
- cached input tokens
- cache write tokens
- output tokens
- audio tokens
- image/video/tool units where applicable
- web search blocks
- container/session minutes if tools are used
- batch/flex/fast/standard multipliers or distinct table rows
- long-context pricing

The catalog should be internal. Users may see estimated engineer-credit cost, but not raw provider routing unless an advanced/debug screen is explicitly enabled.

### 4. Usage Event Schema

Each fulfilled request should create an engineering usage event. It may contain one provider call, many provider calls, or no provider calls.

```json
{
  "event_id": "uuid",
  "user_id": "uuid",
  "app_id": "frustide",
  "conversation_id": "uuid",
  "agent_task_id": "uuid",
  "engineer_persona": "frust_language_engineer",
  "request_type": "build_code",
  "started_at": "2026-09-23T13:30:32Z",
  "completed_at": "2026-09-23T13:30:44Z",
  "backend_calls": [
    {
      "provider": "openai",
      "provider_request_id": "req_xxx",
      "model": "gpt-5.6-luna",
      "endpoint": "responses",
      "usage": {
        "input_tokens": 114603,
        "cached_input_tokens": 0,
        "cache_write_tokens": 0,
        "output_tokens": 2712,
        "reasoning_tokens": 0,
        "total_tokens": 117315
      },
      "provider_cost_usd_micro": 12345
    }
  ],
  "tool_usage": [
    {
      "tool": "frate_build",
      "count": 1,
      "cost_usd_micro": 0
    }
  ],
  "pricing_catalog_version": "2026-09-23.001",
  "base_cost_usd_micro": 12345,
  "service_fee_usd_micro": 3704,
  "user_charge_usd_micro": 16049,
  "balance_after_usd_micro": 24983951,
  "status": "charged"
}
```

### 5. Wallet Ledger

Use an append-only ledger, not a mutable balance-only table.

Ledger event types:

- `payment_authorized`
- `payment_captured`
- `credit_granted`
- `usage_reserved`
- `usage_charge_finalized`
- `usage_reservation_released`
- `refund`
- `admin_adjustment`
- `chargeback_hold`
- `chargeback_loss`

Current balance is computed from the ledger or maintained as a cached projection with reconciliation checks.

### 6. Preauthorization / Reservation

Before accepting an engineering request:

1. Estimate worst-case cost from selected persona, likely tools, context size, max output, and backend route.
2. Reserve that amount from wallet.
3. Execute the Virtual Engineer task.
4. Finalize actual charge from returned provider/tool usage.
5. Release unused reservation.

If the task fails before providers charge, release reservation. If a provider may have charged but response is uncertain, put it into `pending_reconciliation`.

### 7. Reconciliation

Three layers:

1. Immediate per-request billing from provider response usage and internal tool accounting.
2. Hourly/daily aggregation from provider usage APIs, grouped by API key/project/model where possible.
3. Financial reconciliation from provider cost endpoints/dashboards. For OpenAI, use the Costs endpoint/dashboard as the financial source of truth.

If internal cost differs from OpenAI cost beyond tolerance:

- freeze price catalog activation
- create admin review task
- keep serving if safe, or pause LagDaemon Engineer service if difference exceeds emergency threshold

## Chase Payment Flow

### First Implementation

Use hosted checkout or drop-in UI for prepaid top-ups:

1. User chooses top-up amount on LagDaemon.com.
2. LagDaemon backend creates checkout session.
3. Chase/J.P. Morgan returns checkout session token.
4. Browser renders Hosted Payments Page or Drop-in UI.
5. Payment completes.
6. Webhook/notification confirms transaction.
7. LagDaemon records `payment_captured`.
8. Wallet credit becomes available.

This keeps sensitive card collection in Chase/J.P. Morgan hosted/payment components.

### Auto-Refill

Auto-refill needs stored payment credentials and explicit user consent.

Process:

1. Initial cardholder-initiated transaction stores profile/card token.
2. Store Chase/J.P. Morgan consumer profile/payment method identifiers, not raw card.
3. When user balance drops below threshold, perform merchant-initiated transaction using stored credential fields and the original transaction reference.
4. Record authorization/capture and credit ledger only after success.

This must be very clear in the UI:

- refill threshold
- refill amount
- maximum monthly refill cap
- cancel anytime
- receipt emails

## Abuse And Loss Controls

Must-have controls:

- Per-user daily spend cap.
- Per-user concurrent request cap.
- Per-task max provider calls.
- Per-task max tokens.
- Backend model/vendor allowlist by persona and user tier.
- Tool allowlist by app and mode.
- Kill switch for LagDaemon Engineer service.
- OpenAI project hard spend limit.
- OpenAI project alert thresholds.
- Fraud checks on payment/refill behavior.
- Hold newly paid funds if payment risk is high.
- Block service on chargeback until reviewed.

Agentic systems need special handling. A normal chat call is bounded; a coding engineer can recursively spend through planning, file reads, builds, tests, retries, and model calls. LagDaemon Engineer mode should require:

- explicit task budget
- visible estimated max cost
- stop when budget is hit
- resumable continuation after user approves more budget

## Data And Privacy

LagDaemon Engineer mode means user content flows through LagDaemon servers and may then flow to OpenAI or another backend provider selected by LagDaemon.

Required:

- LagDaemon privacy policy update.
- Terms of service update.
- Explain that LagDaemon may process the request locally or send necessary portions to backend AI/tool providers.
- Explain retention for conversation logs.
- Separate BYOK and LagDaemon Engineer data paths.
- User export/delete path for stored conversations where legally possible.
- Abuse logging with minimal necessary retention.

For code agents, logs may contain source code, secrets, file paths, and business data. Add secret scanning/redaction before storing long-term conversation logs where practical.

## Open Questions

1. Does OpenAI require any additional approval for a paid Virtual Engineer service that internally uses OpenAI?
   - The safer legal framing is "paid LagDaemon engineering service" rather than resale of OpenAI access.
   - This needs attorney or OpenAI sales/legal confirmation before launch.

2. Which first backend should the gateway standardize on?
   - Recommendation: Responses API for new agent/tool work.
   - Keep Chat Completions adapter only for legacy/simple chat if needed.
   - Architect the route planner so later vendors can be added without changing the user-facing product.

3. What minimum top-up avoids card fee losses?
   - With online card fees around 2.9% + $0.25, tiny top-ups are bad.
   - $5 minimum is plausible; $10 is healthier.

4. Should model pricing be shown to users?
   - Recommendation: show estimated engineer-credit cost, not raw token tables in normal UI.
   - Advanced page can show details.

5. Can one OpenAI project/API key per LagDaemon app improve reconciliation?
   - Recommendation: yes, at least split production apps by project/key. It makes usage and abuse tracking easier.

## Phase Plan

### Phase 1: Manual Virtual Engineer Wallet Prototype

- Build Virtual Engineer Gateway using OpenAI only behind the scenes.
- Support one or two internal backend routes.
- Add first persona: Frust Language Engineer.
- Use manual admin credit grants, no credit cards yet.
- Record usage events and ledger debits.
- Show balance in FrustIDE.
- Add per-task max budget.
- Reconcile daily against OpenAI usage/costs manually.

### Phase 2: Router, Persona, Pricing Catalog

- Add intent/persona router.
- Add persona-specific context bundles and process cards.
- Build official OpenAI pricing scraper.
- Store versioned catalog.
- Add admin review/activation screen.
- Add pricing unit tests with captured HTML fixtures.
- Add reconciliation report.

### Phase 3: Chase Top-Ups

- Create J.P. Morgan Payments developer account/project.
- Implement hosted checkout or drop-in UI.
- Add payment webhook handler.
- Credit wallet only after confirmed capture.
- Email receipt.

### Phase 4: Auto-Refill

- Add explicit auto-refill consent UI.
- Store payment profile/token identifiers.
- Add user monthly cap.
- Add refill failure handling.
- Add chargeback/hold system.

### Phase 5: Production Hardening

- OpenAI hard spend caps.
- Future provider abstraction.
- Fraud/rate controls.
- Admin dashboard.
- Refund/adjustment tooling.
- Legal review.
- Privacy/terms updates.
- Disaster kill switch.

## Minimal Database Tables

```text
users
engineer_accounts
engineer_wallet_ledger
engineer_usage_events
engineer_personas
engineer_routes
ai_price_catalog_versions
ai_price_catalog_items
provider_accounts
virtual_engineer_requests
payment_customers
payment_methods
payment_transactions
reconciliation_runs
reconciliation_mismatches
```

## Sources

- OpenAI API pricing: https://developers.openai.com/api/docs/pricing
- OpenAI Responses API usage object: https://developers.openai.com/api/reference/cli/resources/responses/methods/create
- OpenAI Organization Usage/Costs API: https://developers.openai.com/api/reference/resources/admin/subresources/organization/subresources/usage
- OpenAI spend limits: https://developers.openai.com/api/docs/guides/spend-limits
- OpenAI Services Agreement: https://openai.com/policies/services-agreement/
- Chase Payment Solutions: https://www.chase.com/business/payments
- J.P. Morgan Online Payments overview: https://developer.payments.jpmorgan.com/docs/commerce/online-payments/capabilities/online-payments
- J.P. Morgan Online Payments getting started: https://developer.payments.jpmorgan.com/docs/commerce/online-payments/getting-started
- J.P. Morgan Checkout session: https://developer.payments.jpmorgan.com/docs/commerce/online-payments/capabilities/checkout/how-to/create-checkout-session
- J.P. Morgan CIT/MIT stored-card transactions: https://developer.payments.jpmorgan.com/docs/commerce/online-payments/capabilities/online-payments/payment-methods/cards/cit-mit-stored
