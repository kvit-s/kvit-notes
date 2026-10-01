package app

// Remote content and the update check.
//
// Opening a note is not consent: a note is an untrusted document, so nothing
// remote loads on sight. Automatic loading is off by default
// (network.autoLoadRemoteContent); with it off an origin loads only after the
// reader approves it, kept in network.allowedOrigins. The update check is the
// one request the app makes without per-origin approval: opt-out daily
// (updates.checkEnabled, updates.lastCheck), no telemetry, no auto-download,
// a passive status-bar notice that opens the release page.

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"
)

// The settings keys.
const (
	autoLoadKey  = "network.autoLoadRemoteContent"
	originsKey   = "network.allowedOrigins"
	updateOnKey  = "updates.checkEnabled"
	updateAtKey  = "updates.lastCheck"
	updateVerKey = "updates.latestVersion"
	updateURLKey = "updates.releaseUrl"
	updateAvailK = "updates.updateAvailable"
)

// appBaseVersion mirrors cmd/kvit-notes/version.go baseVersion.
const appBaseVersion = "2.0.0"

// maxRemoteBytes is the largest remote picture, preview or update answer
// read: a preview thumbnail or an inline image.
const maxRemoteBytes = 8 << 20

// originOf is "https://example.com" for any URL on that origin: scheme, host
// and a non-default port, lowercased. "" when the URL is not a well-formed
// http(s) URL. Consent is per origin, not per URL.
func originOf(raw string) string {
	u, err := url.Parse(strings.TrimSpace(raw))
	if err != nil {
		return ""
	}
	scheme := strings.ToLower(u.Scheme)
	if scheme != "http" && scheme != "https" {
		return ""
	}
	host := strings.ToLower(u.Hostname())
	if host == "" || strings.Contains(host, " ") {
		return ""
	}
	if strings.Contains(u.User.String(), "@") || u.User.String() != "" {
		return ""
	}
	port := u.Port()
	if (scheme == "http" && (port == "" || port == "80")) ||
		(scheme == "https" && (port == "" || port == "443")) {
		return scheme + "://" + host
	}
	if port == "" {
		return scheme + "://" + host
	}
	return scheme + "://" + host + ":" + port
}

// canRequestConsent reports whether a URL is fetchable in principle: right
// scheme, no embedded credentials, a host present. Only consent is missing.
func canRequestConsent(raw string) bool {
	return originOf(raw) != ""
}

// refusalReason says why a URL is not fetchable, "" when it is. Anything
// that is not a well-formed http(s) URL without credentials is refused
// before a request is built.
func refusalReason(raw string) string {
	s := strings.TrimSpace(raw)
	if s == "" {
		return "Not a web address"
	}
	u, err := url.Parse(s)
	if err != nil {
		return "Not a web address"
	}
	scheme := strings.ToLower(u.Scheme)
	if scheme != "http" && scheme != "https" {
		return "Only http and https addresses load"
	}
	if u.User.String() != "" {
		return "Addresses with credentials never load"
	}
	if strings.ToLower(u.Hostname()) == "" {
		return "Not a web address"
	}
	return ""
}

// isSameSiteRedirect reports whether a request that started at approved may
// follow a redirect to target without asking again: the same host, or one a
// subdomain of the other, never a downgrade out of https, a change of port,
// a hop between sibling subdomains, or anything with an address literal.
func isSameSiteRedirect(approved, target string) bool {
	a, errA := url.Parse(approved)
	b, errB := url.Parse(target)
	if errA != nil || errB != nil {
		return false
	}
	if strings.ToLower(a.Scheme) != strings.ToLower(b.Scheme) {
		return false
	}
	if a.Scheme == "https" && b.Scheme == "http" {
		return false
	}
	if a.Port() != b.Port() {
		// Default ports are equal even when one side names them.
		pa, pb := a.Port(), b.Port()
		if !(pa == "" && ((a.Scheme == "http" && pb == "80") || (a.Scheme == "https" && pb == "443"))) &&
			!(pb == "" && ((b.Scheme == "http" && pa == "80") || (b.Scheme == "https" && pa == "443"))) {
			return false
		}
	}
	ah, bh := strings.ToLower(a.Hostname()), strings.ToLower(b.Hostname())
	if ah == "" || bh == "" || ah == bh {
		return ah == bh && ah != ""
	}
	if net.ParseIP(ah) != nil || net.ParseIP(bh) != nil {
		return false
	}
	if isSubdomain(bh, ah) || isSubdomain(ah, bh) {
		// Never toward a parent shorter than two labels.
		parent := ah
		if len(bh) > len(ah) {
			parent = bh
		}
		_ = parent
		shorter := ah
		if len(bh) < len(ah) {
			shorter = bh
		}
		if !strings.Contains(shorter, ".") {
			return false
		}
		return true
	}
	return false
}

func isSubdomain(child, parent string) bool {
	return strings.HasSuffix(child, "."+parent)
}

// egressPolicy is the reader's remote-content decisions, over prefs.
type egressPolicy struct {
	p *prefs
}

func newEgress(p *prefs) *egressPolicy { return &egressPolicy{p: p} }

// autoLoad reports the master switch.
func (e *egressPolicy) autoLoad() bool { return e.p.bool(autoLoadKey, false) }

func (e *egressPolicy) setAutoLoad(on bool) { e.p.set(autoLoadKey, on) }

// allowedOrigins lists the approved origins, sorted.
func (e *egressPolicy) allowedOrigins() []string {
	out := e.p.strings(originsKey)
	seen := map[string]bool{}
	var clean []string
	for _, o := range out {
		o = strings.ToLower(strings.TrimSpace(o))
		if o == "" || seen[o] {
			continue
		}
		seen[o] = true
		clean = append(clean, o)
	}
	sort.Strings(clean)
	return clean
}

func (e *egressPolicy) isOriginAllowed(raw string) bool {
	o := originOf(raw)
	if o == "" {
		return false
	}
	for _, a := range e.allowedOrigins() {
		if a == o {
			return true
		}
	}
	return false
}

// isAllowed reports whether a URL may be fetched now: well-formed, and the
// master switch is on or its origin is approved.
func (e *egressPolicy) isAllowed(raw string) bool {
	if refusalReason(raw) != "" {
		return false
	}
	if e.autoLoad() {
		return true
	}
	return e.isOriginAllowed(raw)
}

// allowOrigin records the approval of a URL's origin.
func (e *egressPolicy) allowOrigin(raw string) {
	o := originOf(raw)
	if o == "" {
		return
	}
	for _, a := range e.allowedOrigins() {
		if a == o {
			return
		}
	}
	e.p.setStrings(originsKey, append(e.allowedOrigins(), o))
}

// forgetOrigin withdraws the approval of a URL's origin.
func (e *egressPolicy) forgetOrigin(raw string) {
	o := originOf(raw)
	if o == "" {
		return
	}
	var keep []string
	for _, a := range e.allowedOrigins() {
		if a != o {
			keep = append(keep, a)
		}
	}
	e.p.setStrings(originsKey, keep)
}

func (e *egressPolicy) forgetAllOrigins() { e.p.setStrings(originsKey, nil) }

// The editor's RemotePolicy over the same decisions.
func (e *egressPolicy) IsAllowed(url string) bool  { return e.isAllowed(url) }
func (e *egressPolicy) Allow(url string)           { e.allowOrigin(url) }
func (e *egressPolicy) CanRequest(url string) bool { return canRequestConsent(url) }
func (e *egressPolicy) Refusal(url string) string  { return refusalReason(url) }
func (e *egressPolicy) Origin(url string) string   { return originOf(url) }

// addressIsBlocked reports whether the app must never connect to an address:
// every special-use prefix other than ordinary public unicast, plus loopback
// unless tests allow it, multicast, broadcast and the unspecified address,
// and the IPv4-mapped, 6to4, Teredo and NAT64 encapsulations carrying one of
// those inside. A URL in a note is chosen by whoever wrote it, so without
// this the editor can be aimed at the reader's router or credentials.
var allowLoopbackForTests = false

func addressIsBlocked(ip net.IP) bool {
	if ip == nil {
		return true
	}
	if ip.IsUnspecified() || ip.IsMulticast() || ip.IsLinkLocalUnicast() || ip.IsLinkLocalMulticast() {
		return true
	}
	if ip.IsLoopback() {
		return !allowLoopbackForTests
	}
	// Private-use, carrier-grade NAT, documentation, benchmark and other
	// special-use ranges must never be reached from a note.
	if ip.IsPrivate() {
		return true
	}
	// 6to4, Teredo and NAT64 carry an IPv4 address inside: block when the
	// inner address is special-use.
	if ip.To4() != nil {
		return false
	}
	// IPv6 special-use beyond what the helpers cover: documentation,
	// discard, example and limited ranges.
	if strings.HasPrefix(strings.ToLower(ip.String()), "2001:db8") {
		return true
	}
	return false
}

// updateChecker is the disclosed, opt-out update check: on startup, at most
// once a day, a passive status-bar notice when a newer release is found. No
// telemetry, no auto-download.
type updateChecker struct {
	p *prefs
	// now and fetch are seams for tests.
	now   func() time.Time
	check func(current string) (latest, url string, err error)
	// current is the running version.
	current string
}

func newUpdateChecker(p *prefs, current string) *updateChecker {
	return &updateChecker{p: p, now: time.Now, current: current}
}

func (u *updateChecker) enabled() bool { return u.p.bool(updateOnKey, true) }

func (u *updateChecker) setEnabled(on bool) { u.p.set(updateOnKey, on) }

// shouldCheck reports whether the daily check is due: enabled and never
// checked, or last checked over a day ago.
func (u *updateChecker) shouldCheck() bool {
	if !u.enabled() {
		return false
	}
	v, ok := u.p.value(updateAtKey)
	if !ok {
		return true
	}
	var at time.Time
	switch n := v.(type) {
	case string:
		t, err := time.Parse(time.RFC3339, n)
		if err != nil {
			return true
		}
		at = t
	case float64:
		at = time.Unix(int64(n), 0)
	case int:
		at = time.Unix(int64(n), 0)
	default:
		return true
	}
	return u.now().Sub(at) >= 24*time.Hour
}

// markChecked records a check now.
func (u *updateChecker) markChecked() {
	u.p.set(updateAtKey, u.now().Format(time.RFC3339))
}

// latestVersion is the newest release found, "" when none.
func (u *updateChecker) latestVersion() string { return u.p.string(updateVerKey, "") }

// releaseURL opens the release page when the notice is clicked.
func (u *updateChecker) releaseURL() string { return u.p.string(updateURLKey, "") }

// updateAvailable is the notice text, "" when none.
func (u *updateChecker) updateAvailable() string {
	if u.p.bool(updateAvailK, false) && u.latestVersion() != "" {
		return "Update available: v" + u.latestVersion()
	}
	return ""
}

// noteUpdateAvailable records a newer release for the status bar.
func (u *updateChecker) noteUpdateAvailable(latest, releaseURL string) {
	if latest == "" || latest == u.current {
		u.p.set(updateAvailK, false)
		u.p.set(updateVerKey, "")
		return
	}
	u.p.set(updateAvailK, true)
	u.p.set(updateVerKey, latest)
	u.p.set(updateURLKey, releaseURL)
}

func (u *updateChecker) clearUpdate() {
	u.p.set(updateAvailK, false)
	u.p.set(updateVerKey, "")
}

// updateEndpoint is the fixed release feed the opt-out check reads: the
// GitHub Releases latest endpoint. The check is the one request the app makes
// without per-origin approval, enabled by Settings.
const updateEndpoint = "https://api.github.com/repos/kvit-s/kvit-notes/releases/latest"

// maybeCheck runs the daily update check in the background when due: at most
// once a day, stamped before the request so a mid-flight exit still counts,
// never when KVIT_DISABLE_UPDATE_CHECK is set (tests, packaging).
func (u *updateChecker) maybeCheck(done func()) {
	if !u.shouldCheck() {
		if done != nil {
			done()
		}
		return
	}
	if v, ok := os.LookupEnv("KVIT_DISABLE_UPDATE_CHECK"); ok && v != "" {
		if done != nil {
			done()
		}
		return
	}
	u.markChecked()
	go func() {
		latest, page, err := fetchLatestRelease(updateEndpoint, u.current)
		if err == nil && latest != "" {
			u.noteUpdateAvailable(latest, page)
		}
		if done != nil {
			done()
		}
	}()
}

// fetchLatestRelease reads the GitHub releases latest payload and returns the
// newest release when it is newer than current: its version without a leading
// "v" and its page URL. "" when current is newest or the payload does not
// parse.
func fetchLatestRelease(endpoint, current string) (latest, page string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	body, err := fetchGuarded(ctx, endpoint, 64<<10)
	if err != nil {
		return "", "", err
	}
	version, url := parseLatestPayload(body)
	if version == "" {
		return "", "", nil
	}
	if compareVersions(version, strings.TrimPrefix(current, "v")) <= 0 && compareVersions(version, current) <= 0 {
		return "", "", nil
	}
	// Compare against the running version without its -dev suffix.
	base := current
	if i := strings.Index(base, "-"); i >= 0 {
		base = base[:i]
	}
	if compareVersions(version, strings.TrimPrefix(base, "v")) <= 0 {
		return "", "", nil
	}
	return version, url, nil
}

// parseLatestPayload reads a GitHub releases/latest JSON payload: tag_name as
// the version (without a leading "v") and html_url as its page. "" when the
// payload does not parse.
func parseLatestPayload(body []byte) (version, page string) {
	var payload struct {
		TagName string `json:"tag_name"`
		HTMLURL string `json:"html_url"`
	}
	if err := json.Unmarshal(body, &payload); err != nil {
		return "", ""
	}
	v := strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(payload.TagName), "v"))
	if v == "" {
		return "", ""
	}
	return v, strings.TrimSpace(payload.HTMLURL)
}

// compareVersions compares dotted releases with an optional -prerelease
// suffix: negative when a < b, 0 when equal, positive when a > b. A release
// without a suffix beats its prereleases.
func compareVersions(a, b string) int {
	ra, pa := splitPrerelease(strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(a), "v")))
	rb, pb := splitPrerelease(strings.TrimSpace(strings.TrimPrefix(strings.TrimSpace(b), "v")))
	fa, fb := strings.Split(ra, "."), strings.Split(rb, ".")
	for i := 0; i < len(fa) || i < len(fb); i++ {
		var na, nb int
		if i < len(fa) {
			na, _ = strconv.Atoi(strings.TrimSpace(fa[i]))
		}
		if i < len(fb) {
			nb, _ = strconv.Atoi(strings.TrimSpace(fb[i]))
		}
		if na != nb {
			return na - nb
		}
	}
	if pa == pb {
		return 0
	}
	if pa == "" {
		return 1
	}
	if pb == "" {
		return -1
	}
	if pa < pb {
		return -1
	}
	return 1
}

func splitPrerelease(v string) (release, pre string) {
	if i := strings.Index(v, "-"); i >= 0 {
		return v[:i], v[i+1:]
	}
	return v, ""
}

// guardedTransport connects only to ordinary public unicast: every request a
// note causes travels over it, which asks the policy again once DNS has
// resolved and once more after each redirect.
func guardedTransport() *http.Transport {
	base := http.DefaultTransport.(*http.Transport).Clone()
	base.DialContext = func(ctx context.Context, network, addr string) (net.Conn, error) {
		d := &net.Dialer{}
		conn, err := d.DialContext(ctx, network, addr)
		if err != nil {
			return nil, err
		}
		if host, _, err := net.SplitHostPort(addr); err == nil {
			if ips, err := net.DefaultResolver.LookupIP(ctx, "ip", host); err == nil {
				for _, ip := range ips {
					if addressIsBlocked(ip) {
						conn.Close()
						return nil, &net.AddrError{Err: "address blocked", Addr: ip.String()}
					}
				}
			}
		}
		return conn, nil
	}
	return base
}

var guardedClient = &http.Client{Transport: guardedTransport(), Timeout: 10 * time.Second}

// fetchGuarded reads at most limit bytes of an address over the guarded
// transport, following only same-site redirects without asking again.
func fetchGuarded(ctx context.Context, address string, limit int64) ([]byte, error) {
	if refusalReason(address) != "" {
		return nil, &url.Error{Op: "Get", URL: address, Err: errRefused(refusalReason(address))}
	}
	initial := originOf(address)
	client := guardedClient
	// Follow redirects manually to enforce the same-site rule.
	for redirects := 0; redirects < 5; redirects++ {
		req, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
		if err != nil {
			return nil, err
		}
		req.Header.Set("User-Agent", "Kvit Notes")
		resp, err := client.Do(req)
		if err != nil {
			return nil, err
		}
		body, readErr := io.ReadAll(io.LimitReader(resp.Body, limit+1))
		loc := resp.Header.Get("Location")
		status := resp.StatusCode
		resp.Body.Close()
		if status/100 == 2 {
			if int64(len(body)) > limit {
				return nil, &url.Error{Op: "Get", URL: address, Err: errTooLarge()}
			}
			return body, readErr
		}
		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && loc != "" {
			next := loc
			if base, err := url.Parse(address); err == nil {
				if ref, err := base.Parse(loc); err == nil {
					next = ref.String()
				}
			}
			if originOf(next) != initial && !isSameSiteRedirect(initial, next) {
				return nil, &url.Error{Op: "Get", URL: next, Err: errRedirectNeedsApproval()}
			}
			address = next
			continue
		}
		return nil, &url.Error{Op: "Get", URL: address, Err: errStatus(status)}
	}
	return nil, &url.Error{Op: "Get", URL: address, Err: errTooManyRedirects()}
}

func errRefused(reason string) error { return &refusalError{reason} }

type refusalError struct{ reason string }

func (e *refusalError) Error() string { return e.reason }

func errTooLarge() error { return &simpleError{"response larger than the 8 MB budget"} }

func errRedirectNeedsApproval() error {
	return &simpleError{"redirect needs approval"}
}

func errTooManyRedirects() error { return &simpleError{"too many redirects"} }

func errStatus(status int) error { return &simpleError{http.StatusText(status)} }

type simpleError struct{ s string }

func (e *simpleError) Error() string { return e.s }
