package minisign

import (
	"bytes"
	"crypto/ed25519"
	"crypto/rand"
	"errors"
	"os"
	"path/filepath"
	"testing"
)

// a key of the tests, as the project has one
func testKey(t *testing.T) (ed25519.PrivateKey, PublicKey) {
	t.Helper()
	public, private, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	var id [8]byte
	rand.Read(id[:])
	return private, PublicKey{ID: id, Key: public}
}

// A signature of the key over the file, with its comment, passes; the file, the comment or the
// signature changed, or another key, do not.
func TestVerify(t *testing.T) {
	private, public := testKey(t)
	_, other := testKey(t)
	data := []byte("abc  nxpress.exe\n")
	signature := Sign(private, public.ID, data, "NXDock 0.1.0")
	if comment, err := Verify(data, signature, []PublicKey{other, public}); err != nil || comment != "NXDock 0.1.0" {
		t.Fatalf("good signature: %q %v", comment, err)
	}
	for what, check := range map[string]func() error{
		"another file": func() error {
			_, err := Verify([]byte("abd  nxpress.exe\n"), signature, []PublicKey{public})
			return err
		},
		"another key": func() error { _, err := Verify(data, signature, []PublicKey{other}); return err },
		"no key":      func() error { _, err := Verify(data, signature, nil); return err },
		"comment changed": func() error {
			_, err := Verify(data, bytes.Replace(signature, []byte("0.1.0"), []byte("0.1.1"), 1), []PublicKey{public})
			return err
		},
		"not a signature": func() error { _, err := Verify(data, []byte("hello"), []PublicKey{public}); return err },
	} {
		if err := check(); !errors.Is(err, ErrSignature) {
			t.Errorf("%s: %v", what, err)
		}
	}
	// the key as minisign writes it reads back
	keys, err := ParsePublicKeys("# keys\n" + public.Text())
	if err != nil || len(keys) != 1 || keys[0].ID != public.ID || !keys[0].Key.Equal(public.Key) {
		t.Fatalf("public key read back: %v %v", keys, err)
	}
	// and the private one the maintainer keeps
	again, id, err := ParsePrivateKey(PrivateKeyText(private, public.ID))
	if err != nil || id != public.ID || !again.Equal(private) {
		t.Fatalf("private key read back: %v", err)
	}
}

// Keys reads release.pub from the folder the tools run in, and gives none without it.
func TestKeys(t *testing.T) {
	here, _ := os.Getwd()
	defer os.Chdir(here)
	folder := t.TempDir()
	os.Chdir(folder)
	if keys := Keys(); len(keys) != 0 {
		t.Fatalf("keys without release.pub: %v", keys)
	}
	_, public := testKey(t)
	os.WriteFile(filepath.Join(folder, "release.pub"), []byte("# the keys\n"+public.Text()), 0o644)
	if keys := Keys(); len(keys) != 1 || keys[0].ID != public.ID {
		t.Fatalf("keys of release.pub: %v", keys)
	}
}
