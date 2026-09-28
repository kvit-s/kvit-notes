package app

import (
	"net"
	"os"
	"testing"
	"time"

	kvitui "github.com/kvit-s/kvit-ui"
)

// memUI returns a UI without a settings file, so prefs last for the process.
func memUI(t *testing.T) *kvitui.UI {
	t.Helper()
	ui, err := kvitui.New(kvitui.Options{IgnoreDesktop: true})
	if err != nil {
		t.Fatal(err)
	}
	return ui
}

func TestMain(m *testing.M) {
	// Hermetic tests: never touch the network for updates, and serve embeds
	// from loopback as the Qt suite does through its seam.
	os.Setenv("KVIT_DISABLE_UPDATE_CHECK", "1")
	allowLoopbackForTests = true
	os.Exit(m.Run())
}

func TestOriginOf(t *testing.T) {
	cases := map[string]string{
		"https://example.com/page":       "https://example.com",
		"https://example.com:443/x":      "https://example.com",
		"http://example.com:80/x":        "http://example.com",
		"https://example.com:8443/x":     "https://example.com:8443",
		"HTTP://EXAMPLE.COM/x":           "http://example.com",
		"https://user@example.com/":      "",
		"ftp://example.com/x":            "",
		"not a url":                      "",
		"https://":                       "",
		"file:///etc/passwd":             "",
		"javascript:alert(1)":            "",
		"https://example.com/a?b=c#frag": "https://example.com",
	}
	for in, want := range cases {
		if got := originOf(in); got != want {
			t.Errorf("originOf(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestEgressPolicyDefaultsToClosed(t *testing.T) {
	p := newPrefs(memUI(t))
	e := newEgress(p)
	if e.autoLoad() {
		t.Error("auto-load must be off by default: opening a note is not consent")
	}
	if e.isAllowed("https://example.com/page") {
		t.Error("nothing remote may load before its origin is approved")
	}
	if !canRequestConsent("https://example.com/page") {
		t.Error("a well-formed https URL should offer a Load affordance")
	}
	if canRequestConsent("javascript:alert(1)") {
		t.Error("a javascript URL must never offer to load")
	}
	if got := refusalReason("javascript:alert(1)"); got == "" {
		t.Error("a javascript URL needs a refusal reason for the card")
	}
}

func TestEgressPolicyPerOriginConsent(t *testing.T) {
	p := newPrefs(memUI(t))
	e := newEgress(p)
	e.allowOrigin("https://example.com/a")
	e.allowOrigin("https://example.com/b")
	if got := e.allowedOrigins(); len(got) != 1 || got[0] != "https://example.com" {
		t.Errorf("consent is per origin, got %v", got)
	}
	if !e.isAllowed("https://example.com/other") {
		t.Error("an approved origin must load")
	}
	if e.isAllowed("https://evil.example.com/") {
		t.Error("a sibling subdomain must not inherit approval")
	}
	e.forgetOrigin("https://example.com/x")
	if e.isAllowed("https://example.com/x") {
		t.Error("forgetting must withdraw approval")
	}
	e.allowOrigin("https://a.example/")
	e.allowOrigin("https://b.example/")
	e.forgetAllOrigins()
	if len(e.allowedOrigins()) != 0 || e.isAllowed("https://a.example/") {
		t.Error("forget-all must clear every origin")
	}
	e.setAutoLoad(true)
	if !e.isAllowed("https://any.example/page") {
		t.Error("the master switch opts into loading without asking")
	}
}

func TestSameSiteRedirect(t *testing.T) {
	if !isSameSiteRedirect("https://example.com/a", "https://www.example.com/b") {
		t.Error("the naked-domain-to-www hop must not ask again")
	}
	if !isSameSiteRedirect("https://www.example.com/a", "https://example.com/b") {
		t.Error("the www-to-naked hop must not ask again")
	}
	if isSameSiteRedirect("https://example.com/", "http://example.com/") {
		t.Error("a downgrade out of https must ask again")
	}
	if isSameSiteRedirect("https://a.example.com/", "https://b.example.com/") {
		t.Error("a hop between siblings must ask again")
	}
	if isSameSiteRedirect("https://example.com:443/", "https://example.com:8443/") {
		t.Error("a change of port must ask again")
	}
	if !isSameSiteRedirect("https://127.0.0.1/", "https://127.0.0.1/other") {
		// Exact loopback match is same-site; the address rule still blocks it.
		t.Error("loopback exact match should be same-site (blocked elsewhere)")
	}
}

func TestAddressIsBlocked(t *testing.T) {
	// The suite allows loopback for its hermetic server; this test checks the
	// shipped rule, so turn the seam back off for it.
	old := allowLoopbackForTests
	allowLoopbackForTests = false
	defer func() { allowLoopbackForTests = old }()
	blocked := []string{"127.0.0.1", "10.1.2.3", "192.168.1.1", "172.16.0.5", "169.254.169.254", "::1", "224.0.0.1"}
	for _, s := range blocked {
		if !addressIsBlocked(net.ParseIP(s)) {
			t.Errorf("%s must never be connected to from a note", s)
		}
	}
	if addressIsBlocked(net.ParseIP("93.184.216.34")) {
		t.Error("ordinary public unicast must be reachable once approved")
	}
}

func TestUpdateCheckIsOptOutDailyAndPassive(t *testing.T) {
	p := newPrefs(memUI(t))
	u := newUpdateChecker(p, "2.0.0")
	fixed := time.Date(2026, 9, 28, 12, 0, 0, 0, time.UTC)
	u.now = func() time.Time { return fixed }
	if !u.enabled() {
		t.Error("the update check is on by default and opts out in Settings")
	}
	if !u.shouldCheck() {
		t.Error("with no check recorded, the daily check is due")
	}
	u.markChecked()
	if u.shouldCheck() {
		t.Error("just checked: must not check again until a day passes")
	}
	u.now = func() time.Time { return fixed.Add(25 * time.Hour) }
	if !u.shouldCheck() {
		t.Error("over a day later: the check is due again")
	}
	u.setEnabled(false)
	if u.shouldCheck() {
		t.Error("opted out: must never check")
	}
	u.setEnabled(true)
	u.noteUpdateAvailable("2.1.0", "https://example.com/releases/2.1.0")
	if got := u.updateAvailable(); got == "" {
		t.Error("a newer release must show a passive notice")
	}
	u.noteUpdateAvailable("2.0.0", "")
	if got := u.updateAvailable(); got != "" {
		t.Errorf("the current version is not an update: %q", got)
	}
}

func TestCompareVersions(t *testing.T) {
	cases := []struct {
		a, b string
		want int
	}{
		{"2.1.0", "2.0.0", 1},
		{"2.0.0", "2.1.0", -1},
		{"2.0.0", "2.0.0", 0},
		{"v2.1.0", "2.1.0", 0},
		{"2.0.0-rc1", "2.0.0", -1},
		{"2.0.0", "2.0.0-rc1", 1},
		{"2.0.10", "2.0.9", 1},
		{"2.0.0-rc2", "2.0.0-rc1", 1},
	}
	for _, c := range cases {
		got := compareVersions(c.a, c.b)
		sign := 0
		if got < 0 {
			sign = -1
		} else if got > 0 {
			sign = 1
		}
		if sign != c.want {
			t.Errorf("compareVersions(%q, %q) = %d, want sign %d", c.a, c.b, got, c.want)
		}
	}
}

func TestParseLatestPayload(t *testing.T) {
	v, page := parseLatestPayload([]byte(`{"tag_name":"v2.1.0","html_url":"https://example.com/r/2.1.0"}`))
	if v != "2.1.0" || page != "https://example.com/r/2.1.0" {
		t.Errorf("payload: %q %q", v, page)
	}
	if v, _ := parseLatestPayload([]byte(`{}`)); v != "" {
		t.Errorf("empty payload should not parse: %q", v)
	}
	if v, _ := parseLatestPayload([]byte(`not json`)); v != "" {
		t.Errorf("bad json should not parse: %q", v)
	}
}
