# Windows code signing and the SmartScreen warning

Written 2026-08-14, against the state of the tree at `v1.0.0-rc3`.

## What this is about

Kvit Notes ships three Windows artifacts from a release tag: a per-user Inno
Setup installer (`Kvit_Notes-<version>-setup.exe`), a portable zip, and the
checksums beside them. None of them carries an Authenticode signature. A user
who downloads the installer therefore meets Microsoft Defender SmartScreen's
"Windows protected your PC" dialog and has to click "More info" and then
"Run anyway" before the installer runs at all.

That is recorded rather than accidental. The README states it under the
download table, `docs/qa-checklist.md` step 5 of the Windows section expects
exactly one such warning and treats it as a pass, and ADR 0007 lists Windows
Authenticode signing under "Absent". macOS took the opposite decision, because
Gatekeeper *blocks* an unsigned application rather than warning about it, so
the macOS DMG is Developer ID-signed, notarized and stapled, and Gatekeeper
verification blocks the packaging job.

This document collects what the choice actually involves: how the warning is
produced, what each way of removing it costs, what the project already decided,
and what has changed since that decision was written. It exists because the
subject is full of stale folklore, including advice that was correct until 2024
and is now wrong, and because the decision has a lead time measured in weeks,
so it cannot be made on the day it is needed.

## How SmartScreen decides, in one section

SmartScreen weighs two signals when a downloaded file runs:

1. **Publisher reputation**: is the file signed, and is the signing identity
   one it has seen behave well?
2. **File hash reputation**: has this exact file been downloaded and run by
   enough people without trouble?

Two consequences follow, and between them they explain everything else here.

An **unsigned** file has only the second signal, and the hash changes with
every build. Reputation therefore starts at zero for every release and never
accumulates across them: rc3 warns, 1.0.0 warns, 1.0.1 warns, indefinitely.
Microsoft's own wording is that reputation "cannot transfer from previous
versions unless both were signed using the same publisher identity".

A **signed** file adds the first signal, and that one is durable. Once an
identity has accrued reputation, later releases signed by the same identity
inherit it. What that buys is an end state where releases stop warning instead
of resetting each time, rather than silence on the first download.

On how long that takes, Microsoft declines to give a threshold and says only
that it "can take several weeks and hundreds of clean installs from a wide
audience". Volume alone is not the whole input: users clicking through are a
positive signal and users abandoning the download are a negative one.

Two beliefs that were true once and are false now:

- **An EV certificate no longer bypasses SmartScreen.** That behaviour was
  removed in 2024. EV and OV certificates now build reputation identically, so
  paying the EV premium for SmartScreen alone buys nothing. Microsoft states
  this outright in its code-signing guidance.
- **Every code signing certificate now needs hardware.** Since June 2023 the
  CA/Browser Forum requires the private key of an OV certificate to live on a
  hardware token or an HSM, so the traditional CA route also means receiving
  and guarding a physical token.

## What is already decided

`~/kvit-notes-pro/m2-owner.md`, dated 2026-07-23, records the decision:
Windows builds will be signed with **Azure Artifact Signing** (the service
Microsoft first shipped as Azure Code Signing, renamed to Trusted Signing and
then to Artifact Signing), and the subscription is not started until the
launch is otherwise ready. Release candidates stay unsigned, so the first
signed artifact is the one the wider audience downloads. The same document
sequences it as a sub-item of step 12, the soft launch.

The reasoning was that the two facts above remove the case for an EV
certificate, and that a service holding the key in Microsoft's HSM removes the
hardware token from the picture entirely.

Nothing in either repository records a decision about the Microsoft Store.
`docs/release-rollback.md` and ADR 0007 mention winget and a Homebrew tap as
channels that are not live yet; the Store is not discussed. The Store section
below is therefore new ground rather than a revision.

## Azure Artifact Signing in detail

**You never receive a certificate.** This is the part that most resembles a
misunderstanding waiting to happen. The service issues a fresh certificate at
signing time and keeps it: "The Authenticode certificate that's used for
signing with the profile is never given to you. All certificates are securely
stored within the service and are accessible only at the time of signing." The
public certificate is embedded in each signed binary. There is no `.pfx` to
export and no offline signing.

The pieces you create are an Artifact Signing account, an **identity
validation**, and a **certificate profile**. Signing then happens by calling
the service, through `signtool` with the Artifact Signing dlib, a first-party
GitHub Action, or the SDK, authenticated with Azure credentials.

Mechanics worth knowing before committing:

- A new certificate is issued daily and each is valid for about **three days**.
  An RFC 3161 timestamp is therefore not optional: the timestamp is what keeps
  a signature valid long after the certificate behind it expired. The service's
  timestamp endpoint is `http://timestamp.acs.microsoft.com`.
- It requires a **paid** Azure subscription. Free, trial and sponsored
  subscriptions are refused at resource creation.
- Billing is **not pro-rated**: the full monthly SKU is invoiced regardless of
  when in the month you start signing. Recorded price is $9.99/month for the
  Basic tier, up to 5,000 signatures on one certificate profile; confirm at the
  pricing page rather than from this note.
- The certificate subject cannot be customised. No custom Common Name or
  Organization value is possible, because the CA/Browser Forum requires the
  validated legal name.
- Artifact Signing resources **cannot be migrated** across subscriptions,
  tenants or resource groups. Changing either identifier means recreating
  everything.
- Identity validation itself expires. Reminders start 60 days out, and if it
  lapses, certificate renewal stops and signing stops with it.

### Eligibility, and the three-year rule that does not apply

The much-quoted requirement that a subscriber have three years of verifiable
history applies to the **organization** path only. Individuals validate
differently: by presenting a government-issued ID to a third-party verifier
(AU10TIX) and a Microsoft Verified ID credential in the Authenticator app,
with a recent utility bill or bank statement for the address.

Current geography, from the quickstart page as of 2026-08-11: Public Trust
certificates are available to organizations in the United States, Canada, the
European Union, the United Kingdom, Australia, New Zealand, Japan, South Korea,
Singapore, Switzerland, Norway and Israel, while **individual developers must
be located in the United States or Canada**.

Practical points for the individual path:

- The billing account type must match the validation type. An Individual
  billing account can only validate an individual identity, and the identity
  form is populated read-only from it, so corrections have to be made in the
  billing account first, including casing and hyphenation, for which the FAQ
  gives an `az billing account update` command.
- The certificate subject shows the legal name plus city, state/province and
  country. Street address and email are not included.
- **Processing takes 1 to 20 business days**, longer if Microsoft requests more
  documentation, and there are only three upload attempts before the request
  fails for good. Start two to four weeks before the build that needs it.
- Microsoft has paused individual onboarding during the preview before, and
  there are current Q&A threads from people unable to create an individual
  validation. The documentation describes the flow as live, and the only way to
  establish whether the portal currently accepts an individual request is to
  submit one.

### The open problem: intermediate CA rotation

Since late March 2026 the service rotates customers through new intermediate
certificate authorities (AOC CA 03, EOC CA 03 and 04, AOC CA 04). Because its
certificates are short-lived and reissued for nearly every release, each binary
can end up under a fresh intermediate whose own reputation has not
accumulated, and SmartScreen weighs the issuing CA's trust signal alongside
the file hash. Customers with a year of clean releases began getting warnings
on every release.

A Microsoft engineer confirmed this as a known platform issue on 2026-06-26,
tracked in a public GitHub issue, with submission of file hashes to the
Microsoft Security Intelligence portal as the short-term workaround. Two
Microsoft documents disagree about whether such submission is even a supported
mechanism for consumer endpoints; in practice it is what affected customers are
using.

**This has to be re-checked before the subscription is started.** While it
persists, $9.99/month buys the verified publisher name and the durable identity
but may not buy a quiet install.

### If the subscription stops

Everything already signed stays good. Deleting a certificate profile "doesn't
revoke any previously issued certificates or invalidate their signatures", and
deleting the account "doesn't affect the certificates that were already used to
sign your files". Combined with the timestamp, a signed installer keeps
verifying indefinitely and keeps whatever reputation it earned. Revocation is
reserved for confirmed misuse or abuse.

What stops is the ability to sign anything new: the next release goes out
unsigned, starting from zero, unable to inherit the identity's reputation.

### If an LLC is formed later

Nothing forces a move. Signing can continue as an individual indefinitely; the
cost is that every installer's publisher line shows the owner's personal legal
name, which cannot be customised.

Moving the publisher to a company means a new organization identity validation
and a new certificate subject, and Microsoft's guidance is that changing the
signing certificate affects the publisher trust signal, with reputation
transferring only within the same publisher identity. The company name would
start from zero.

Two constraints make that worse than it sounds. The organization path is where
the three-year verifiable-history requirement lives, so a newly formed LLC
could not onboard for roughly three years, and the Q&A threads asking about an
exception process have none to point to. And since resources cannot migrate
across subscriptions or tenants, a company setup means recreating the account,
the validation and the profile against a new organization billing account.

The consequence is that the individual route is the right first move rather
than a compromise: it is available now, a company would not be eligible for
years, and the switch, if it ever happens, can be made deliberately, paying
the reputation reset then or simply continuing to sign personally.

## The alternatives, compared

| Option | Cost | SmartScreen on first download | Notes |
|---|---|---|---|
| Microsoft Store, MSIX | Free | **None** | Microsoft re-signs after certification; no certificate to buy or renew |
| Microsoft Store, own EXE/MSI | Cost of a certificate | None during a Store install | Microsoft does **not** re-sign; a Trusted Root Program CA certificate is required |
| Azure Artifact Signing | ~$9.99/month | Warns until reputation accrues | No hardware token; US/Canada for individuals; CA rotation issue above |
| OV certificate (DigiCert, Sectigo, Certum, SSL.com) | ~$150–300/year | Warns until reputation accrues | Hardware token or HSM required since June 2023 |
| EV certificate | $400+/year | Same as OV since 2024 | No longer justified for SmartScreen alone |
| SignPath Foundation | Free for qualifying open-source projects | Same as OV | Microsoft's own guidance points open-source projects here |
| Unsigned (today) | Free | Warns, every release, forever | Enterprise policy may block continuation entirely |

Two entries deserve more than a table row.

**SignPath Foundation** offers OV-level signing free to qualifying open-source
projects, and `launch-plan.md` already carries it as post-launch upside with
the right caveat: their terms require an already-released project, a written
code-signing policy, named signing roles with multi-factor authentication,
verifiable builds and manual per-release approval, and their rules on
affiliated proprietary code must be checked against the planned paid tier
before committing. That last point is the one to resolve first, since this
repository is the open product and the agent module is not.

**The Microsoft Store** is the only route that removes the warning by
construction rather than by waiting. Registration is now free (individuals
since September 2025, companies since May 2026), and for a future paid tier,
non-game apps may use their own in-app commerce and keep 100% of revenue. An
MSIX submission is re-signed by Microsoft, gets free hosting, OS-managed
updates every 24 hours, S Mode support and Windows 11 backup-and-restore
reinstall. Listing the existing `.exe` instead ("bring your own installer")
keeps the current artifact and the current hosting, but still requires a
certificate, so it does not avoid the spend.

What an MSIX would cost in work, for this codebase specifically:

- A fourth Windows artifact in CI, built from the same `windeployqt` staging
  tree `packaging/windows/build-windows.ps1` already produces for the portable
  zip and the installer.
- The `.md` association moves out of the `HKCU` ProgID keys in
  `packaging/windows/kvit-notes.iss` and into the package manifest.
- **The in-app update check has to be disabled in a Store build.** The OS owns
  updates there and an app that fetches its own conflicts with Store policy, so
  this needs a build-time flag and an About dialog that stays truthful about
  which channel it came from.
- Certification per release (listing, age rating, privacy policy, review)
  rather than a tag push that publishes itself.

Nothing in the codebase blocks it: a packaged Win32 app runs full-trust, so
vaults in arbitrary folders, the advisory `VaultLock` and the file watcher all
behave as they do now, with `%AppData%` and `HKCU` writes redirected rather
than denied.

The catch that decides its priority: a Store listing only covers people who
install from the Store. An audience arriving from Hacker News, Reddit or the
README clicks the GitHub download, and that file behaves exactly as it does
today.

## What signing buys even before reputation accrues

Worth stating because it is easy to score this purely on the first-download
dialog:

- The dialog and the file properties name a **verified publisher** instead of
  an unknown one.
- Antivirus and endpoint-detection heuristics treat an unsigned, freshly built
  installer far more suspiciously. False-positive quarantines are common for
  unsigned Qt applications that unpack a tree of DLLs.
- Managed environments running AppLocker or Windows Defender Application
  Control frequently refuse unsigned binaries outright, so some users cannot
  click through at all.
- Chrome and Edge apply their own download reputation before SmartScreen is
  reached, and an unsigned installer is likelier to be flagged or blocked
  there.

One benefit that does *not* apply here: the "Unknown publisher" line in the
User Account Control dialog. The installer is per-user
(`PrivilegesRequired=lowest`), so it never asks for administrator rights and
that prompt is not in the path.

## Wiring, when the credentials exist

The signing step belongs in the existing `package-windows` job in
`.github/workflows/ci.yml`, which already runs on `windows-2022`, enters the
protected `release` environment on a tag, and calls
`packaging/windows/build-windows.ps1`. The Azure credentials go into that same
environment, beside the Apple notarization credential, so pull requests and
arbitrary manual refs cannot reach them.

Two ordering points for whoever implements it. The application binaries must be
signed in the staging tree *before* Inno Setup packages them, and the resulting
`setup.exe` signed afterwards, so that both the installer and what it installs
are signed. And `docs/qa-checklist.md` already expects a
`signtool verify` check in its distribution section, so the checklist's
SmartScreen step (currently "Builds ship unsigned for now") is edited in the
same change.

## Plan of record

1. Release candidates ship unsigned. Unchanged, and the README and QA checklist
   already say so.
2. Before 1.0.0, re-check the intermediate-CA rotation issue. If it is still
   producing a warning on every release, the subscription buys the verified
   publisher name and little else, and the decision to start it can wait.
3. Start individual identity validation two to four weeks ahead of the build
   that needs it, not on release day, and treat the portal's acceptance of an
   individual request as the thing to verify first.
4. If individual onboarding is unavailable, fall back to an OV certificate from
   Certum or SSL.com, or to SignPath Foundation if the affiliated-proprietary-code
   question resolves favourably.
5. Treat a Microsoft Store MSIX listing as a post-launch candidate on its own
   merits. It is the only zero-cost route to a clean install, and it hedges the
   risk in step 2, but it covers Store installs only, so it supplements the
   download path rather than replacing it.

## Sources

Checked 2026-08-14.

- [Code signing options for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options): the comparison table, EV's removed bypass, Store re-signing
- [SmartScreen reputation for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation): the two signals, "several weeks and hundreds of clean installs", reputation transfer
- [Artifact Signing FAQ](https://learn.microsoft.com/en-us/azure/artifact-signing/faq): certificates never released to the subscriber, paid-subscription requirement, non-prorated billing, unenrollment, no custom CN/O, no resource migration
- [Quickstart: Set up Artifact Signing](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart): supported countries, individual validation flow, 1–20 business days
- [Artifact Signing certificate management](https://learn.microsoft.com/en-us/azure/artifact-signing/concept-certificate-management): daily issuance, three-day validity, timestamping
- [SmartScreen on every release with Artifact Signing (Microsoft Q&A)](https://learn.microsoft.com/en-us/answers/questions/5929307/artifact-signing-account-is-giving-smartscreen-at): the CA rotation issue, confirmed 2026-06-26
- [How to distribute your Win32 application through Microsoft Store](https://learn.microsoft.com/en-us/windows/apps/distribute-through-store/how-to-distribute-your-win32-app-through-microsoft-store): MSIX versus own-installer comparison
- [Free developer registration for individual developers](https://blogs.windows.com/windowsdeveloper/2025/09/10/free-developer-registration-for-individual-developers-on-microsoft-store/) and [for companies](https://blogs.windows.com/windowsdeveloper/2026/05/07/publish-to-microsoft-store-as-a-company-now-with-free-registration-and-faster-onboarding/)

In this repository and beside it: `README.md` download section,
`docs/qa-checklist.md` Windows steps, `docs/adr/0007-supported-release-platforms.md`,
`packaging/windows/kvit-notes.iss`, `.github/workflows/ci.yml` `package-windows`
job, `~/kvit-notes-pro/m2-owner.md` "Windows code signing", and
`~/kvit-notes-pro/launch-plan.md` section D1.
