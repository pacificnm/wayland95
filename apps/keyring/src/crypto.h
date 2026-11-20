#pragma once

#include <QByteArray>

// The keyring's cryptography, all OpenSSL (libcrypto):
//  - the password list file: a key from the password by scrypt, then
//    AES-256-GCM over the contents (so a wrong password, or a changed file,
//    fails to open rather than giving rubbish);
//  - the Secret Service's "dh-ietf1024-sha256-aes128-cbc-pkcs7" sessions,
//    which libsecret asks for so secrets don't cross D-Bus in the clear:
//    Diffie-Hellman over RFC 2409's 1024-bit group, HKDF-SHA256 to a
//    128-bit key, AES-128-CBC with PKCS#7 padding.
namespace Crypto {

QByteArray randomBytes(int count);

// scrypt (N = 2^logN, r, p) -> a 32-byte key; empty on failure
QByteArray deriveKey(const QByteArray &password, const QByteArray &salt, int logN, int r, int p);

// AES-256-GCM: ciphertext followed by its 16-byte tag
QByteArray seal(const QByteArray &key, const QByteArray &nonce, const QByteArray &aad, const QByteArray &plain);
// false when the key is wrong or the data was changed
bool open(const QByteArray &key, const QByteArray &nonce, const QByteArray &aad, const QByteArray &sealed,
          QByteArray *plain);

// SHA-256 (for the attribute hashes a locked list can still be searched by)
QByteArray sha256(const QByteArray &data);

// overwrite before letting go
void wipe(QByteArray &data);

// One side of a dh-ietf1024 session
class DhSession
{
public:
    // From the program's public key: makes ours and the shared AES key.
    // False if theirs isn't a valid one.
    bool start(const QByteArray &peerPublic);
    QByteArray publicKey() const { return m_public; }

    // AES-128-CBC/PKCS#7 with a fresh IV (returned in `iv`)
    QByteArray encrypt(const QByteArray &plain, QByteArray *iv) const;
    bool decrypt(const QByteArray &iv, const QByteArray &cipher, QByteArray *plain) const;

    ~DhSession() { wipe(m_key); }

private:
    QByteArray m_public;
    QByteArray m_key;
};

} // namespace Crypto
