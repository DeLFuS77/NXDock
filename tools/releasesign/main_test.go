package main

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/DeLFuS77/NXDock/tools/minisign"
)

const (
	testCommit = "1111111111111111111111111111111111111111"
	testTagObj = "2222222222222222222222222222222222222222"
)

// fakeRelease is a draft on GitHub as the workflow leaves it, and the gh and git that see it.
type fakeRelease struct {
	files      map[string][]byte
	draft      bool
	localTag   string
	remoteTag  string
	unattested string
	attested   [][]string
	uploaded   []byte
	published  bool
	keyAsked   bool
}

func newFakeRelease() *fakeRelease {
	f := &fakeRelease{draft: true, localTag: testCommit, remoteTag: testCommit, files: map[string][]byte{
		"NXDock.nro":                    []byte("the program of Windows"),
		"NXDock-0.1.0-linux-x64.tar.gz": []byte("the program of Linux"),
	}}
	sums := ""
	for _, name := range []string{"NXDock.nro", "NXDock-0.1.0-linux-x64.tar.gz"} {
		sum := sha256.Sum256(f.files[name])
		sums += hex.EncodeToString(sum[:]) + "  " + name + "\n"
	}
	f.files[sumsName] = []byte(sums)
	f.files["NXDock-0.1.0.intoto.jsonl"] = []byte("{}")
	return f
}

func (f *fakeRelease) tools(secret string, yes bool, out *bytes.Buffer) tools {
	return tools{
		out: out,
		git: func(args ...string) ([]byte, error) {
			switch args[0] {
			case "rev-parse":
				if f.localTag == "" {
					return nil, errors.New("no tag")
				}
				return []byte(f.localTag + "\n"), nil
			case "ls-remote":
				return []byte(fmt.Sprintf("%s\trefs/tags/0.1.0\n%s\trefs/tags/0.1.0^{}\n", testTagObj, f.remoteTag)), nil
			}
			return nil, fmt.Errorf("git %v", args)
		},
		gh: func(args ...string) ([]byte, error) {
			switch strings.Join(args[:2], " ") {
			case "release view":
				r := map[string]any{"isDraft": f.draft}
				assets := []map[string]string{}
				for name := range f.files {
					assets = append(assets, map[string]string{"name": name})
				}
				r["assets"] = assets
				return json.Marshal(r)
			case "release download":
				folder := args[slices.Index(args, "-D")+1]
				for name, data := range f.files {
					os.WriteFile(filepath.Join(folder, name), data, 0o644)
				}
				return nil, nil
			case "attestation verify":
				f.attested = append(f.attested, args)
				if filepath.Base(args[2]) == f.unattested {
					return nil, errors.New("no attestation")
				}
				return nil, nil
			case "release upload":
				data, err := os.ReadFile(args[3])
				f.uploaded = data
				return nil, err
			case "release edit":
				f.published = true
				return nil, nil
			}
			return nil, fmt.Errorf("gh %v", args)
		},
		readKey: func(string) (string, error) {
			f.keyAsked = true
			return secret, nil
		},
		confirm: func(string) bool { return yes },
	}
}

func testKey() (string, minisign.PublicKey) {
	public, private, _ := ed25519.GenerateKey(rand.Reader)
	var id [8]byte
	rand.Read(id[:])
	return minisign.PrivateKeyText(private, id), minisign.PublicKey{ID: id, Key: public}
}

var draft = options{repo: defaultRepo, workflow: defaultWorkflow, version: "0.1.0"}

// A draft whose files match their checksums and were built from the commit of the tag is signed
// for its version with the key of NXDock, and published once told so; the key is never shown.
func TestSignADraft(t *testing.T) {
	secret, key := testKey()
	for _, yes := range []bool{true, false} {
		f := newFakeRelease()
		var out bytes.Buffer
		if err := run(f.tools(secret, yes, &out), draft, []minisign.PublicKey{key}); err != nil {
			t.Fatal(err)
		}
		comment, err := minisign.Verify(f.files[sumsName], f.uploaded, []minisign.PublicKey{key})
		if err != nil || comment != "NXDock 0.1.0" {
			t.Fatalf("signature: %q %v", comment, err)
		}
		if f.published != yes {
			t.Errorf("published %v, told %v", f.published, yes)
		}
		if len(f.attested) != 2 {
			t.Fatalf("%d files checked by their provenance", len(f.attested))
		}
		for _, want := range []string{"--signer-workflow", defaultRepo + "/" + defaultWorkflow, "--source-ref", "refs/tags/0.1.0",
			"--source-digest", testCommit, "--deny-self-hosted-runners"} {
			if !slices.Contains(f.attested[0], want) {
				t.Errorf("the provenance is not checked with %s: %v", want, f.attested[0])
			}
		}
		if strings.Contains(out.String(), secret) {
			t.Error("the key was printed")
		}
	}
}

// Anything that does not hold stops before the key is asked, or before the signature goes in.
func TestSignRefuses(t *testing.T) {
	secret, key := testKey()
	other, _ := testKey()
	keys := []minisign.PublicKey{key}
	for what, c := range map[string]struct {
		change   func(f *fakeRelease, o *options, keys *[]minisign.PublicKey, secret *string)
		want     string
		keyAsked bool
	}{
		"a file changed after its checksum": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.files["NXDock.nro"] = []byte("something else")
		}, "does not match its checksum", false},
		"a file not in the checksums": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.files["NXDock-0.1.0-macos-apple-silicon.tar.gz"] = []byte("added")
		}, "not in SHA256SUMS.txt", false},
		"a checksum of a file the release lacks": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			delete(f.files, "NXDock-0.1.0-linux-x64.tar.gz")
		}, "which the release does not have", false},
		"a file not built by the workflow from the tag": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.unattested = "NXDock-0.1.0-linux-x64.tar.gz"
		}, "was not built by", false},
		"no tag here": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.localTag = ""
		}, "is not in this repository", false},
		"another tag on GitHub": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.remoteTag = "3333333333333333333333333333333333333333"
		}, "is not the one of this repository", false},
		"a release published already": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.draft = false
		}, "is published already", false},
		"a release signed already": {func(f *fakeRelease, _ *options, _ *[]minisign.PublicKey, _ *string) {
			f.files[signatureName] = []byte("old")
		}, "has a signature already", false},
		"not a version": {func(_ *fakeRelease, o *options, _ *[]minisign.PublicKey, _ *string) {
			o.version = "latest"
		}, "is not the tag of a version", false},
		"a release.pub without keys": {func(_ *fakeRelease, _ *options, k *[]minisign.PublicKey, _ *string) {
			*k = nil
		}, "has no public key", false},
		"not a key": {func(_ *fakeRelease, _ *options, _ *[]minisign.PublicKey, s *string) {
			*s = "hello"
		}, "not a private key", true},
		"a key not in release.pub": {func(_ *fakeRelease, _ *options, _ *[]minisign.PublicKey, s *string) {
			*s = other
		}, "not one of release.pub", true},
	} {
		f := newFakeRelease()
		o, k, s := draft, keys, secret
		c.change(f, &o, &k, &s)
		var out bytes.Buffer
		err := run(f.tools(s, true, &out), o, k)
		if err == nil || !strings.Contains(err.Error(), c.want) {
			t.Errorf("%s: %v", what, err)
		}
		if f.uploaded != nil || f.published {
			t.Errorf("%s: signed or published", what)
		}
		if f.keyAsked != c.keyAsked {
			t.Errorf("%s: key asked %v", what, f.keyAsked)
		}
		if strings.Contains(out.String(), s) || err != nil && strings.Contains(err.Error(), s) {
			t.Errorf("%s: the key was printed", what)
		}
	}
}

// A release published before it was signed, or one signed again, when told so.
func TestSignAPublishedRelease(t *testing.T) {
	secret, key := testKey()
	f := newFakeRelease()
	f.draft = false
	f.files[signatureName] = []byte("old")
	o := draft
	o.published, o.replace = true, true
	var out bytes.Buffer
	if err := run(f.tools(secret, true, &out), o, []minisign.PublicKey{key}); err != nil {
		t.Fatal(err)
	}
	if _, err := minisign.Verify(f.files[sumsName], f.uploaded, []minisign.PublicKey{key}); err != nil || f.published {
		t.Errorf("signature %v, published again %v", err, f.published)
	}
}

func TestPeeled(t *testing.T) {
	for listing, want := range map[string]string{
		"aaa\trefs/tags/0.1.0\nbbb\trefs/tags/0.1.0^{}\n": "bbb",
		"aaa\trefs/tags/0.1.0\n":                          "aaa",
		"":                                                "",
		"ccc\trefs/tags/0.1.00\n":                         "",
	} {
		if got := peeled(listing, "refs/tags/0.1.0"); got != want {
			t.Errorf("%q: %q, not %q", listing, got, want)
		}
	}
}

// -check tells whether a key kept in a password manager is the one of release.pub, without
// showing it.
func TestCheckKey(t *testing.T) {
	secret, key := testKey()
	other, _ := testKey()
	for given, ok := range map[string]bool{secret: true, other: false, "hello": false} {
		var out bytes.Buffer
		err := checkKey(tools{out: &out, readKey: func(string) (string, error) { return given, nil }}, []minisign.PublicKey{key})
		if (err == nil) != ok {
			t.Errorf("%v: %v", ok, err)
		}
		if strings.Contains(out.String(), given) || err != nil && strings.Contains(err.Error(), given) {
			t.Error("the key was printed")
		}
	}
	if err := checkKey(tools{readKey: func(string) (string, error) { return secret, nil }}, nil); err == nil {
		t.Error("a release.pub without keys")
	}
}

// The ID -check tells is the one release.pub shows: the first key and a spare one are told apart.
func TestKeyName(t *testing.T) {
	keys, err := minisign.ParsePublicKeys("untrusted comment: minisign public key 64300306F44900F2\nRWTyAEn0BgMwZEHfYfKbB2GIONHfe8HIu2rAcGUFe2fss7QzuqAIx0JO\n")
	if err != nil || len(keys) != 1 {
		t.Fatal(err)
	}
	if name := keyName(keys[0].ID); name != "64300306F44900F2" {
		t.Errorf("name %s", name)
	}
}
