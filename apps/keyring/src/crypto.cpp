#include "crypto.h"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>

#include <memory>

namespace Crypto {

namespace {
struct CipherCtxFree { void operator()(EVP_CIPHER_CTX *c) const { EVP_CIPHER_CTX_free(c); } };
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxFree>;
struct BnFree { void operator()(BIGNUM *b) const { BN_clear_free(b); } };
using Bn = std::unique_ptr<BIGNUM, BnFree>;
struct BnCtxFree { void operator()(BN_CTX *c) const { BN_CTX_free(c); } };
struct PkeyCtxFree { void operator()(EVP_PKEY_CTX *c) const { EVP_PKEY_CTX_free(c); } };

const unsigned char *u(const QByteArray &a) { return reinterpret_cast<const unsigned char *>(a.constData()); }
unsigned char *u(QByteArray &a) { return reinterpret_cast<unsigned char *>(a.data()); }

// a big number as exactly `size` big-endian bytes
QByteArray toBytes(const BIGNUM *n, int size)
{
    QByteArray out(size, '\0');
    if (BN_bn2binpad(n, u(out), size) != size)
        return {};
    return out;
}
} // namespace

QByteArray randomBytes(int count)
{
    QByteArray out(count, '\0');
    if (RAND_bytes(u(out), count) != 1)
        qFatal("win95keyring: the system's random numbers aren't available");
    return out;
}

void wipe(QByteArray &data)
{
    if (!data.isEmpty())
        OPENSSL_cleanse(data.data(), data.size());
    data.clear();
}

QByteArray deriveKey(const QByteArray &password, const QByteArray &salt, int logN, int r, int p)
{
    if (logN < 10 || logN > 22 || r < 1 || r > 32 || p < 1 || p > 16)
        return {};
    QByteArray key(32, '\0');
    const uint64_t N = uint64_t(1) << logN;
    // (128 * N * r bytes of memory, plus room)
    const uint64_t maxMem = 128 * N * uint64_t(r) * uint64_t(p) + (16u << 20);
    if (EVP_PBE_scrypt(password.constData(), password.size(), u(salt), salt.size(), N, r, p, maxMem, u(key),
                       key.size()) != 1)
        return {};
    return key;
}

QByteArray seal(const QByteArray &key, const QByteArray &nonce, const QByteArray &aad, const QByteArray &plain)
{
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    int len = 0;
    QByteArray out(plain.size() + 16, '\0');
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1
        || EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, u(key), u(nonce)) != 1
        || EVP_EncryptUpdate(ctx.get(), nullptr, &len, u(aad), aad.size()) != 1
        || EVP_EncryptUpdate(ctx.get(), u(out), &len, u(plain), plain.size()) != 1)
        return {};
    int total = len;
    if (EVP_EncryptFinal_ex(ctx.get(), u(out) + total, &len) != 1)
        return {};
    total += len;
    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, u(out) + total) != 1)
        return {};
    out.resize(total + 16);
    return out;
}

bool open(const QByteArray &key, const QByteArray &nonce, const QByteArray &aad, const QByteArray &sealed,
          QByteArray *plain)
{
    if (sealed.size() < 16)
        return false;
    const int cipherLen = sealed.size() - 16;
    QByteArray tag = sealed.right(16);
    QByteArray out(cipherLen, '\0');
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    int len = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1
        || EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, u(key), u(nonce)) != 1
        || EVP_DecryptUpdate(ctx.get(), nullptr, &len, u(aad), aad.size()) != 1
        || EVP_DecryptUpdate(ctx.get(), u(out), &len, u(sealed), cipherLen) != 1
        || EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, u(tag)) != 1) {
        wipe(out);
        return false;
    }
    int total = len;
    if (EVP_DecryptFinal_ex(ctx.get(), u(out) + total, &len) != 1) {
        wipe(out);
        return false;
    }
    out.resize(total + len);
    *plain = out;
    return true;
}

QByteArray sha256(const QByteArray &data)
{
    QByteArray out(32, '\0');
    unsigned int len = 0;
    if (EVP_Digest(data.constData(), data.size(), u(out), &len, EVP_sha256(), nullptr) != 1)
        return {};
    return out;
}

// --- dh-ietf1024-sha256-aes128-cbc-pkcs7 ------------------------------------

bool DhSession::start(const QByteArray &peerPublic)
{
    std::unique_ptr<BN_CTX, BnCtxFree> ctx(BN_CTX_new());
    Bn prime(BN_get_rfc2409_prime_1024(nullptr));
    Bn generator(BN_new());
    Bn priv(BN_new());
    Bn pub(BN_new());
    Bn peer(BN_bin2bn(u(peerPublic), peerPublic.size(), nullptr));
    Bn shared(BN_new());
    Bn limit(BN_new());
    if (!ctx || !prime || !generator || !priv || !pub || !peer || !shared || !limit)
        return false;
    const int primeBytes = BN_num_bytes(prime.get());
    // theirs must be in 2 .. p-2 (not 0, 1 or p-1, which give away the key)
    if (!BN_set_word(generator.get(), 2) || !BN_copy(limit.get(), prime.get()) || !BN_sub_word(limit.get(), 1))
        return false;
    if (BN_cmp(peer.get(), BN_value_one()) <= 0 || BN_cmp(peer.get(), limit.get()) >= 0)
        return false;
    // ours: a random exponent as long as the prime
    if (!BN_priv_rand_range(priv.get(), limit.get()) || BN_is_zero(priv.get())
        || !BN_mod_exp(pub.get(), generator.get(), priv.get(), prime.get(), ctx.get())
        || !BN_mod_exp(shared.get(), peer.get(), priv.get(), prime.get(), ctx.get()))
        return false;
    m_public = toBytes(pub.get(), primeBytes);
    // the shared secret, padded to the prime's length (as gnome-keyring
    // and libsecret do), through HKDF-SHA256 with no salt or info
    QByteArray secret = toBytes(shared.get(), primeBytes);
    if (m_public.isEmpty() || secret.isEmpty())
        return false;
    std::unique_ptr<EVP_PKEY_CTX, PkeyCtxFree> kdf(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
    QByteArray key(16, '\0');
    size_t keyLen = key.size();
    const bool ok = kdf && EVP_PKEY_derive_init(kdf.get()) == 1
                    && EVP_PKEY_CTX_set_hkdf_md(kdf.get(), EVP_sha256()) == 1
                    && EVP_PKEY_CTX_set1_hkdf_key(kdf.get(), u(secret), secret.size()) == 1
                    && EVP_PKEY_derive(kdf.get(), u(key), &keyLen) == 1 && keyLen == 16;
    wipe(secret);
    if (!ok)
        return false;
    m_key = key;
    return true;
}

QByteArray DhSession::encrypt(const QByteArray &plain, QByteArray *iv) const
{
    *iv = randomBytes(16);
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    QByteArray out(plain.size() + 16, '\0');
    int len = 0, total = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr, u(m_key), u(*iv)) != 1
        || EVP_EncryptUpdate(ctx.get(), u(out), &len, u(plain), plain.size()) != 1)
        return {};
    total = len;
    if (EVP_EncryptFinal_ex(ctx.get(), u(out) + total, &len) != 1)
        return {};
    out.resize(total + len);
    return out;
}

bool DhSession::decrypt(const QByteArray &iv, const QByteArray &cipher, QByteArray *plain) const
{
    if (iv.size() != 16 || cipher.isEmpty() || cipher.size() % 16)
        return false;
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    QByteArray out(cipher.size() + 16, '\0');
    int len = 0, total = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr, u(m_key), u(iv)) != 1
        || EVP_DecryptUpdate(ctx.get(), u(out), &len, u(cipher), cipher.size()) != 1) {
        wipe(out);
        return false;
    }
    total = len;
    if (EVP_DecryptFinal_ex(ctx.get(), u(out) + total, &len) != 1) {
        wipe(out);
        return false;
    }
    out.resize(total + len);
    *plain = out;
    return true;
}

} // namespace Crypto
