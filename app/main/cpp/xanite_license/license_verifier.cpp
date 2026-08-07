// Xanite Gold offline license verification.
//
// Everything that decides whether the app may boot lives here rather than in
// Kotlin, so flipping a boolean in smali is not enough to unlock the build.
// The module is deliberately self-contained: SHA-256 and RSA-2048 PKCS#1 v1.5
// *verification* are implemented inline. Verification only ever touches the
// public exponent, so there is no secret to leak through the naive bignum code
// and no reason to drag OpenSSL or nettle into the link.
//
// license.bin layout (big-endian lengths):
//   "XGL1" | u32 payload_len | payload | u32 sig_len | signature
//
// payload is UTF-8, LF-separated, no trailing LF:
//   v1
//   <patreon_id>
//   <hwid_hex>
//   <creation_timestamp_ms>
//   <slots_used>
//   <max_slots>
//
// signature is RSASSA-PKCS1-v1_5(SHA-256(payload)) over those exact bytes. The
// server must reproduce the payload byte-for-byte or verification fails.

#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>  // _exit, used by the release-only signature check

#include <android/log.h>

#define LOG_TAG "XaniteLicense"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

// ---------------------------------------------------------------------------
// Status codes. Mirrored by LicenseNative.kt.
// ---------------------------------------------------------------------------

enum LicenseStatus : jint {
  LICENSE_VALID = 0,
  LICENSE_MISSING = 1,
  LICENSE_MALFORMED = 2,
  LICENSE_BAD_SIGNATURE = 3,
  LICENSE_HWID_MISMATCH = 4,
  LICENSE_INTERNAL_ERROR = 5,
};

// ---------------------------------------------------------------------------
// Public half of the license signing key. The matching private key lives in
// activation-server/license-private.pem and belongs on the server ONLY -
// never in this repo, never in an APK.
//
// Regenerate with `npm run genkeys` in activation-server, then re-embed the
// modulus. Rotating the key invalidates every license already issued, so every
// user would have to re-activate.
//
// An all-zero modulus here means the build was never configured; RsaVerify
// refuses outright in that case, so an unconfigured build fails closed rather
// than accepting anything.
// ---------------------------------------------------------------------------

constexpr size_t kRsaBytes = 256;  // 2048-bit
alignas(4) const uint8_t kRsaModulusBE[kRsaBytes] = {
    0xc1, 0x92, 0xb7, 0x67, 0x2b, 0x8a, 0x16, 0x9a, 0xf1, 0x1c, 0x8d, 0xea,
    0x65, 0x35, 0xba, 0x61, 0xb1, 0xdb, 0x65, 0x72, 0xa8, 0xa6, 0xba, 0x5d,
    0xe5, 0xed, 0xf3, 0xf0, 0x61, 0xed, 0x5f, 0xad, 0x09, 0x21, 0x2a, 0x1a,
    0xfb, 0x6c, 0xe7, 0x44, 0xaf, 0x2c, 0xeb, 0x51, 0x6b, 0x0a, 0x22, 0x0f,
    0x64, 0xa9, 0x9d, 0x7b, 0xe4, 0x57, 0x9e, 0xc8, 0x62, 0x5d, 0x3f, 0xb4,
    0xfd, 0x9b, 0xbd, 0xee, 0xac, 0x5c, 0xc1, 0xdc, 0xd2, 0x7b, 0xd1, 0x0e,
    0xb4, 0x7a, 0x0e, 0xc2, 0x68, 0x07, 0xfc, 0x97, 0x3f, 0x68, 0x32, 0xe3,
    0xbc, 0xab, 0x09, 0xbc, 0x4a, 0x87, 0xfc, 0x4f, 0xe9, 0xc2, 0x7c, 0xd9,
    0x31, 0x49, 0x4f, 0xa6, 0x75, 0x4b, 0xe4, 0x88, 0x47, 0x43, 0xd7, 0xad,
    0x08, 0x49, 0x4a, 0xae, 0xf7, 0x8f, 0xf5, 0xe3, 0x57, 0x2f, 0x4a, 0x08,
    0xc0, 0xf0, 0x45, 0x72, 0x09, 0x84, 0x45, 0xf1, 0x88, 0x79, 0x59, 0x9d,
    0x03, 0x99, 0xdf, 0x09, 0x86, 0x7b, 0x2f, 0xc4, 0xfc, 0xf3, 0xdb, 0xfc,
    0x83, 0x80, 0xdf, 0x9e, 0x5e, 0x05, 0x33, 0xe1, 0xd3, 0xf0, 0x88, 0x45,
    0x04, 0x0c, 0xb6, 0x27, 0x53, 0x19, 0x1f, 0x2d, 0x97, 0x4b, 0x98, 0x76,
    0xd3, 0xa7, 0xe3, 0xb5, 0x34, 0x9a, 0x48, 0x62, 0x80, 0xeb, 0x9e, 0xa9,
    0x6d, 0xca, 0x35, 0xfe, 0x94, 0xbf, 0x56, 0x7d, 0x9e, 0x24, 0xa7, 0x52,
    0xd8, 0xb4, 0x47, 0x75, 0x6c, 0xee, 0xb8, 0x2e, 0xeb, 0xc4, 0x3c, 0xd6,
    0xcc, 0xa8, 0x89, 0x98, 0x21, 0xce, 0x11, 0x7a, 0xee, 0xbf, 0xf8, 0xca,
    0xeb, 0x0f, 0x69, 0xa3, 0xab, 0xae, 0x51, 0xf4, 0x4b, 0x04, 0x2d, 0x9c,
    0x85, 0x19, 0xc8, 0x7d, 0x7c, 0xbb, 0x20, 0x00, 0x78, 0x31, 0x68, 0xfe,
    0x62, 0xcc, 0xd4, 0xf1, 0xc7, 0xd8, 0xd2, 0x0f, 0x82, 0x9b, 0x85, 0x31,
    0xdc, 0x36, 0x66, 0x17,
};
constexpr uint32_t kRsaPublicExponent = 65537;

// SHA-256 of the release signing certificate, lowercase hex. Obtain with:
//   apksigner verify --print-certs app.apk
// Enforced only in release builds (see XANITE_ENFORCE_APK_SIGNATURE).
const char* const kExpectedCertSha256 =
    "527f6eb59054f9b0a4f8c661f9b78a9fe80cf4a9ffaff43e0a3727b16226446f";

// Salt mixed into the HWID derivation. Changing it invalidates every issued
// license, so it must match ACTIVATION_HWID_SALT on the server.
const char* const kHwidSalt = "xanite-gold-hwid-v1";

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------

struct Sha256 {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t buf[64];
  size_t buflen;
};

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void Sha256Transform(Sha256* ctx, const uint8_t* data) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = (uint32_t(data[i * 4]) << 24) | (uint32_t(data[i * 4 + 1]) << 16) |
           (uint32_t(data[i * 4 + 2]) << 8) | uint32_t(data[i * 4 + 3]);
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2],
           d = ctx->state[3], e = ctx->state[4], f = ctx->state[5],
           g = ctx->state[6], h = ctx->state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + kK[i] + w[i];
    uint32_t s0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
  ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void Sha256Init(Sha256* ctx) {
  ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
  ctx->bitlen = 0;
  ctx->buflen = 0;
}

void Sha256Update(Sha256* ctx, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    ctx->buf[ctx->buflen++] = data[i];
    if (ctx->buflen == 64) {
      Sha256Transform(ctx, ctx->buf);
      ctx->bitlen += 512;
      ctx->buflen = 0;
    }
  }
}

void Sha256Final(Sha256* ctx, uint8_t out[32]) {
  size_t i = ctx->buflen;
  ctx->bitlen += uint64_t(ctx->buflen) * 8;
  ctx->buf[i++] = 0x80;
  if (i > 56) {
    while (i < 64) ctx->buf[i++] = 0;
    Sha256Transform(ctx, ctx->buf);
    i = 0;
  }
  while (i < 56) ctx->buf[i++] = 0;
  for (int b = 7; b >= 0; b--) ctx->buf[i++] = uint8_t(ctx->bitlen >> (b * 8));
  Sha256Transform(ctx, ctx->buf);
  for (int j = 0; j < 8; j++) {
    out[j * 4] = uint8_t(ctx->state[j] >> 24);
    out[j * 4 + 1] = uint8_t(ctx->state[j] >> 16);
    out[j * 4 + 2] = uint8_t(ctx->state[j] >> 8);
    out[j * 4 + 3] = uint8_t(ctx->state[j]);
  }
}

void Sha256Buf(const uint8_t* data, size_t len, uint8_t out[32]) {
  Sha256 ctx;
  Sha256Init(&ctx);
  Sha256Update(&ctx, data, len);
  Sha256Final(&ctx, out);
}

std::string ToHex(const uint8_t* data, size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s.push_back(kHex[data[i] >> 4]);
    s.push_back(kHex[data[i] & 0x0f]);
  }
  return s;
}

// ---------------------------------------------------------------------------
// Minimal bignum, sized for RSA-2048. Little-endian 32-bit limbs.
// Verification is public-key only and runs once per boot, so the naive
// shift-and-subtract reduction is entirely adequate.
// ---------------------------------------------------------------------------

constexpr size_t kLimbs = kRsaBytes / 4;      // 64
constexpr size_t kProdLimbs = kLimbs * 2 + 2; // headroom for the shifted remainder

struct Big {
  uint32_t d[kProdLimbs];
};

void BigZero(Big* a) { memset(a->d, 0, sizeof(a->d)); }

void BigFromBE(Big* a, const uint8_t* be, size_t len) {
  BigZero(a);
  for (size_t i = 0; i < len; i++) {
    size_t limb = (len - 1 - i) / 4;
    size_t shift = ((len - 1 - i) % 4) * 8;
    if (limb < kProdLimbs) a->d[limb] |= uint32_t(be[i]) << shift;
  }
}

void BigToBE(const Big* a, uint8_t* be, size_t len) {
  for (size_t i = 0; i < len; i++) {
    size_t limb = (len - 1 - i) / 4;
    size_t shift = ((len - 1 - i) % 4) * 8;
    be[i] = limb < kProdLimbs ? uint8_t(a->d[limb] >> shift) : 0;
  }
}

// Returns <0, 0, >0 comparing the first `n` limbs.
int BigCmp(const Big* a, const Big* b, size_t n) {
  for (size_t i = n; i-- > 0;) {
    if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
  }
  return 0;
}

// a -= b over `n` limbs. Caller guarantees a >= b.
void BigSub(Big* a, const Big* b, size_t n) {
  uint64_t borrow = 0;
  for (size_t i = 0; i < n; i++) {
    uint64_t cur = uint64_t(a->d[i]) - b->d[i] - borrow;
    a->d[i] = uint32_t(cur);
    borrow = (cur >> 63) & 1;
  }
}

void BigShl1(Big* a, size_t n) {
  uint32_t carry = 0;
  for (size_t i = 0; i < n; i++) {
    uint32_t next = a->d[i] >> 31;
    a->d[i] = (a->d[i] << 1) | carry;
    carry = next;
  }
}

// out = a * b, schoolbook. a and b are kLimbs wide; out is 2*kLimbs.
void BigMul(const Big* a, const Big* b, Big* out) {
  BigZero(out);
  for (size_t i = 0; i < kLimbs; i++) {
    uint64_t carry = 0;
    for (size_t j = 0; j < kLimbs; j++) {
      uint64_t cur = uint64_t(a->d[i]) * b->d[j] + out->d[i + j] + carry;
      out->d[i + j] = uint32_t(cur);
      carry = cur >> 32;
    }
    size_t k = i + kLimbs;
    while (carry && k < kProdLimbs) {
      uint64_t cur = uint64_t(out->d[k]) + carry;
      out->d[k] = uint32_t(cur);
      carry = cur >> 32;
      k++;
    }
  }
}

// value %= mod, by binary long division. `value` spans 2*kLimbs limbs.
void BigMod(Big* value, const Big* mod) {
  Big rem;
  BigZero(&rem);
  const size_t bits = kLimbs * 2 * 32;
  for (size_t i = bits; i-- > 0;) {
    BigShl1(&rem, kLimbs + 1);
    uint32_t bit = (value->d[i / 32] >> (i % 32)) & 1;
    rem.d[0] |= bit;
    if (BigCmp(&rem, mod, kLimbs + 1) >= 0) BigSub(&rem, mod, kLimbs + 1);
  }
  BigZero(value);
  memcpy(value->d, rem.d, (kLimbs + 1) * sizeof(uint32_t));
}

// out = base^65537 mod mod. Exponent is fixed, so this is 16 squarings plus a
// single multiply rather than a general ladder.
void BigModExpF4(const Big* base, const Big* mod, Big* out) {
  Big acc = *base;
  Big prod;
  for (int i = 0; i < 16; i++) {
    BigMul(&acc, &acc, &prod);
    BigMod(&prod, mod);
    acc = prod;
  }
  BigMul(&acc, base, &prod);
  BigMod(&prod, mod);
  *out = prod;
}

// RSASSA-PKCS1-v1_5 verify with SHA-256. The modulus is a parameter purely so
// the self-test can drive it with a generated key; production always passes
// kRsaModulusBE.
bool RsaVerifyWithModulus(const uint8_t* sig, size_t sig_len, const uint8_t digest[32],
                          const uint8_t* modulus_be) {
  if (sig_len != kRsaBytes) return false;

  // An all-zero modulus means the build was never configured with a real key.
  bool key_configured = false;
  for (size_t i = 0; i < kRsaBytes; i++) {
    if (modulus_be[i] != 0) { key_configured = true; break; }
  }
  if (!key_configured) {
    LOGW("no signing key compiled in; refusing to validate");
    return false;
  }
  if (kRsaPublicExponent != 65537) return false;

  Big s, n, m;
  BigFromBE(&s, sig, sig_len);
  BigFromBE(&n, modulus_be, kRsaBytes);
  if (BigCmp(&s, &n, kLimbs) >= 0) return false;  // s must be < n
  BigModExpF4(&s, &n, &m);

  uint8_t em[kRsaBytes];
  BigToBE(&m, em, kRsaBytes);

  // EM = 0x00 0x01 PS(0xFF...) 0x00 DigestInfo(SHA-256) || H
  static const uint8_t kDigestInfo[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
                                        0x60, 0x86, 0x48, 0x01, 0x65, 0x03,
                                        0x04, 0x02, 0x01, 0x05, 0x00, 0x04,
                                        0x20};
  const size_t kTailLen = sizeof(kDigestInfo) + 32;
  if (em[0] != 0x00 || em[1] != 0x01) return false;
  size_t i = 2;
  while (i < kRsaBytes && em[i] == 0xff) i++;
  if (i < 10) return false;              // PS must be >= 8 bytes
  if (i >= kRsaBytes || em[i] != 0x00) return false;
  i++;
  if (kRsaBytes - i != kTailLen) return false;
  if (memcmp(em + i, kDigestInfo, sizeof(kDigestInfo)) != 0) return false;

  // Constant-time-ish compare; the values are public but it costs nothing.
  uint8_t diff = 0;
  const uint8_t* got = em + i + sizeof(kDigestInfo);
  for (size_t j = 0; j < 32; j++) diff = uint8_t(diff | (got[j] ^ digest[j]));
  return diff == 0;
}

bool RsaVerify(const uint8_t* sig, size_t sig_len, const uint8_t digest[32]) {
  return RsaVerifyWithModulus(sig, sig_len, digest, kRsaModulusBE);
}

// ---------------------------------------------------------------------------
// JNI helpers
// ---------------------------------------------------------------------------

std::string JStringToStd(JNIEnv* env, jstring s) {
  if (s == nullptr) return std::string();
  const char* chars = env->GetStringUTFChars(s, nullptr);
  if (chars == nullptr) return std::string();
  std::string out(chars);
  env->ReleaseStringUTFChars(s, chars);
  return out;
}

bool ClearPending(JNIEnv* env) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return true;
  }
  return false;
}

// Settings.Secure.getString(context.getContentResolver(), "android_id")
std::string GetSsaid(JNIEnv* env, jobject context) {
  jclass ctx_cls = env->GetObjectClass(context);
  jmethodID get_resolver = env->GetMethodID(ctx_cls, "getContentResolver",
                                            "()Landroid/content/ContentResolver;");
  if (get_resolver == nullptr) { ClearPending(env); return {}; }
  jobject resolver = env->CallObjectMethod(context, get_resolver);
  if (ClearPending(env) || resolver == nullptr) return {};

  jclass secure_cls = env->FindClass("android/provider/Settings$Secure");
  if (secure_cls == nullptr) { ClearPending(env); return {}; }
  jmethodID get_string = env->GetStaticMethodID(
      secure_cls, "getString",
      "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;");
  if (get_string == nullptr) { ClearPending(env); return {}; }
  jstring key = env->NewStringUTF("android_id");
  auto value = jstring(env->CallStaticObjectMethod(secure_cls, get_string, resolver, key));
  env->DeleteLocalRef(key);
  if (ClearPending(env)) return {};
  return JStringToStd(env, value);
}

// context.getFilesDir().getAbsolutePath()
std::string GetFilesDir(JNIEnv* env, jobject context) {
  jclass ctx_cls = env->GetObjectClass(context);
  jmethodID get_files = env->GetMethodID(ctx_cls, "getFilesDir", "()Ljava/io/File;");
  if (get_files == nullptr) { ClearPending(env); return {}; }
  jobject file = env->CallObjectMethod(context, get_files);
  if (ClearPending(env) || file == nullptr) return {};
  jclass file_cls = env->GetObjectClass(file);
  jmethodID get_path = env->GetMethodID(file_cls, "getAbsolutePath", "()Ljava/lang/String;");
  if (get_path == nullptr) { ClearPending(env); return {}; }
  auto path = jstring(env->CallObjectMethod(file, get_path));
  if (ClearPending(env)) return {};
  return JStringToStd(env, path);
}

// Widevine DRM device id: new MediaDrm(WIDEVINE_UUID)
//                             .getPropertyByteArray("deviceUniqueId")
//
// This identifies the physical handset. Unlike Settings.Secure.ANDROID_ID it is
// NOT scoped to the app's signing key, so a debug build, a release build and a
// reinstall all see the same value. That distinction matters: with SSAID a
// single phone burned one device slot per signing key, which is exactly how
// this account ended up holding two entries for one device.
std::string GetWidevineDeviceId(JNIEnv* env) {
  jclass uuid_cls = env->FindClass("java/util/UUID");
  if (uuid_cls == nullptr) { ClearPending(env); return {}; }
  jmethodID uuid_ctor = env->GetMethodID(uuid_cls, "<init>", "(JJ)V");
  if (uuid_ctor == nullptr) { ClearPending(env); return {}; }
  // The Widevine scheme UUID, edef8ba9-79d6-4ace-a3c8-27dcd51d21ed.
  jobject uuid = env->NewObject(uuid_cls, uuid_ctor,
                                static_cast<jlong>(0xEDEF8BA979D64ACELL),
                                static_cast<jlong>(0xA3C827DCD51D21EDLL));
  if (ClearPending(env) || uuid == nullptr) return {};

  jclass drm_cls = env->FindClass("android/media/MediaDrm");
  if (drm_cls == nullptr) { ClearPending(env); return {}; }
  jmethodID drm_ctor = env->GetMethodID(drm_cls, "<init>", "(Ljava/util/UUID;)V");
  if (drm_ctor == nullptr) { ClearPending(env); return {}; }
  // Throws UnsupportedSchemeException where Widevine is absent; the caller
  // falls back to the SSAID rather than refusing to derive an id at all.
  jobject drm = env->NewObject(drm_cls, drm_ctor, uuid);
  if (ClearPending(env) || drm == nullptr) return {};

  jmethodID get_prop = env->GetMethodID(drm_cls, "getPropertyByteArray",
                                        "(Ljava/lang/String;)[B");
  std::string result;
  if (get_prop != nullptr) {
    jstring prop = env->NewStringUTF("deviceUniqueId");
    auto bytes = jbyteArray(env->CallObjectMethod(drm, get_prop, prop));
    env->DeleteLocalRef(prop);
    if (!ClearPending(env) && bytes != nullptr) {
      jsize len = env->GetArrayLength(bytes);
      if (len > 0) {
        std::vector<uint8_t> buf(static_cast<size_t>(len));
        env->GetByteArrayRegion(bytes, 0, len, reinterpret_cast<jbyte*>(buf.data()));
        result = ToHex(buf.data(), buf.size());
      }
      env->DeleteLocalRef(bytes);
    }
  } else {
    ClearPending(env);
  }

  // close() on API 28+, release() before it. Try both; a leaked MediaDrm would
  // otherwise hold a DRM session open for the life of the process.
  jmethodID close = env->GetMethodID(drm_cls, "close", "()V");
  if (close != nullptr) {
    env->CallVoidMethod(drm, close);
  } else {
    ClearPending(env);
    jmethodID release = env->GetMethodID(drm_cls, "release", "()V");
    if (release != nullptr) env->CallVoidMethod(drm, release);
  }
  ClearPending(env);
  env->DeleteLocalRef(drm);
  env->DeleteLocalRef(uuid);
  return result;
}

std::string DeriveHwid(JNIEnv* env, jobject context) {
  // Prefer the hardware-backed id; tag the material so a device that later
  // gains or loses Widevine cannot collide with its own SSAID-derived id.
  std::string widevine = GetWidevineDeviceId(env);
  if (!widevine.empty()) {
    std::string material = std::string(kHwidSalt) + "|wv|" + widevine;
    uint8_t digest[32];
    Sha256Buf(reinterpret_cast<const uint8_t*>(material.data()), material.size(), digest);
    return ToHex(digest, sizeof(digest));
  }

  std::string ssaid = GetSsaid(env, context);
  if (ssaid.empty()) return {};
  std::string material = std::string(kHwidSalt) + "|ssaid|" + ssaid;
  uint8_t digest[32];
  Sha256Buf(reinterpret_cast<const uint8_t*>(material.data()), material.size(), digest);
  return ToHex(digest, sizeof(digest));
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > 64 * 1024) { fclose(f); return false; }
  out->resize(size_t(size));
  size_t got = fread(out->data(), 1, size_t(size), f);
  fclose(f);
  if (got != size_t(size)) { out->clear(); return false; }
  return true;
}

uint32_t ReadU32BE(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) |
         uint32_t(p[3]);
}

struct LicensePayload {
  std::string patreon_id;
  std::string hwid;
  std::string created_ms;
  std::string slots_used;
  std::string max_slots;
};

bool ParsePayload(const std::string& text, LicensePayload* out) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) { lines.push_back(text.substr(start)); break; }
    lines.push_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  if (lines.size() != 6 || lines[0] != "v1") return false;
  out->patreon_id = lines[1];
  out->hwid = lines[2];
  out->created_ms = lines[3];
  out->slots_used = lines[4];
  out->max_slots = lines[5];
  return !out->patreon_id.empty() && !out->hwid.empty();
}

// Verifies license.bin and, on success, fills `payload`.
jint VerifyLicense(JNIEnv* env, jobject context, LicensePayload* payload) {
  std::string dir = GetFilesDir(env, context);
  if (dir.empty()) return LICENSE_INTERNAL_ERROR;
  std::vector<uint8_t> blob;
  if (!ReadFile(dir + "/license.bin", &blob)) return LICENSE_MISSING;

  if (blob.size() < 4 + 4 + 1 + 4) return LICENSE_MALFORMED;
  if (memcmp(blob.data(), "XGL1", 4) != 0) return LICENSE_MALFORMED;
  size_t off = 4;
  uint32_t payload_len = ReadU32BE(blob.data() + off);
  off += 4;
  if (payload_len == 0 || off + payload_len + 4 > blob.size()) return LICENSE_MALFORMED;
  const uint8_t* payload_bytes = blob.data() + off;
  off += payload_len;
  uint32_t sig_len = ReadU32BE(blob.data() + off);
  off += 4;
  if (off + sig_len != blob.size()) return LICENSE_MALFORMED;

  uint8_t digest[32];
  Sha256Buf(payload_bytes, payload_len, digest);
  if (!RsaVerify(blob.data() + off, sig_len, digest)) return LICENSE_BAD_SIGNATURE;

  std::string text(reinterpret_cast<const char*>(payload_bytes), payload_len);
  if (!ParsePayload(text, payload)) return LICENSE_MALFORMED;

  std::string local = DeriveHwid(env, context);
  if (local.empty()) return LICENSE_INTERNAL_ERROR;
  if (local.size() != payload->hwid.size()) return LICENSE_HWID_MISMATCH;
  uint8_t diff = 0;
  for (size_t i = 0; i < local.size(); i++) {
    diff = uint8_t(diff | (uint8_t(local[i]) ^ uint8_t(payload->hwid[i])));
  }
  if (diff != 0) return LICENSE_HWID_MISMATCH;

  return LICENSE_VALID;
}

// SHA-256 of the APK's signing certificate, lowercase hex.
std::string GetSigningCertSha256(JNIEnv* env, jobject context) {
  jclass ctx_cls = env->GetObjectClass(context);
  jmethodID get_pm = env->GetMethodID(ctx_cls, "getPackageManager",
                                      "()Landroid/content/pm/PackageManager;");
  jmethodID get_pkg = env->GetMethodID(ctx_cls, "getPackageName", "()Ljava/lang/String;");
  if (get_pm == nullptr || get_pkg == nullptr) { ClearPending(env); return {}; }
  jobject pm = env->CallObjectMethod(context, get_pm);
  if (ClearPending(env) || pm == nullptr) return {};
  auto pkg = jstring(env->CallObjectMethod(context, get_pkg));
  if (ClearPending(env) || pkg == nullptr) return {};

  jclass pm_cls = env->GetObjectClass(pm);
  jmethodID get_info = env->GetMethodID(
      pm_cls, "getPackageInfo",
      "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
  if (get_info == nullptr) { ClearPending(env); return {}; }
  // GET_SIGNING_CERTIFICATES; minSdk is 29 so this is always available.
  jobject info = env->CallObjectMethod(pm, get_info, pkg, jint(0x08000000));
  if (ClearPending(env) || info == nullptr) return {};

  jclass info_cls = env->GetObjectClass(info);
  jfieldID signing_fid = env->GetFieldID(info_cls, "signingInfo",
                                         "Landroid/content/pm/SigningInfo;");
  if (signing_fid == nullptr) { ClearPending(env); return {}; }
  jobject signing = env->GetObjectField(info, signing_fid);
  if (ClearPending(env) || signing == nullptr) return {};

  jclass signing_cls = env->GetObjectClass(signing);
  jmethodID get_signers = env->GetMethodID(signing_cls, "getApkContentsSigners",
                                           "()[Landroid/content/pm/Signature;");
  if (get_signers == nullptr) { ClearPending(env); return {}; }
  auto signers = jobjectArray(env->CallObjectMethod(signing, get_signers));
  if (ClearPending(env) || signers == nullptr) return {};
  if (env->GetArrayLength(signers) < 1) return {};

  jobject sig0 = env->GetObjectArrayElement(signers, 0);
  if (ClearPending(env) || sig0 == nullptr) return {};
  jclass sig_cls = env->GetObjectClass(sig0);
  jmethodID to_bytes = env->GetMethodID(sig_cls, "toByteArray", "()[B");
  if (to_bytes == nullptr) { ClearPending(env); return {}; }
  auto bytes = jbyteArray(env->CallObjectMethod(sig0, to_bytes));
  if (ClearPending(env) || bytes == nullptr) return {};

  jsize len = env->GetArrayLength(bytes);
  if (len <= 0) return {};
  std::vector<uint8_t> der(static_cast<size_t>(len));
  env->GetByteArrayRegion(bytes, 0, len, reinterpret_cast<jbyte*>(der.data()));
  if (ClearPending(env)) return {};

  uint8_t digest[32];
  Sha256Buf(der.data(), der.size(), digest);
  return ToHex(digest, sizeof(digest));
}

}  // namespace

#ifdef XANITE_LICENSE_SELFTEST
// Standalone harness: validates SHA-256, the RSA-2048 PKCS#1 v1.5 verify path
// and the container parser against licenses produced by the Node signer, with
// no Android runtime involved.
//
//   selftest <modulus.bin> <license.bin> <expected-hwid>
//
// Exits 0 only when the license verifies AND the HWID matches.
int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <modulus.bin> <license.bin> <expected-hwid>\n", argv[0]);
    return 2;
  }
  // Self-check the hash first: a broken SHA-256 would fail everything below
  // for the wrong reason. Known-answer test for "abc".
  {
    uint8_t d[32];
    Sha256Buf(reinterpret_cast<const uint8_t*>("abc"), 3, d);
    if (ToHex(d, 32) !=
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
      fprintf(stderr, "FAIL sha256 known-answer\n");
      return 1;
    }
  }

  std::vector<uint8_t> modulus, blob;
  if (!ReadFile(argv[1], &modulus) || modulus.size() != kRsaBytes) {
    fprintf(stderr, "FAIL modulus read (%zu bytes)\n", modulus.size());
    return 1;
  }
  if (!ReadFile(argv[2], &blob)) {
    fprintf(stderr, "FAIL license read\n");
    return 1;
  }

  if (blob.size() < 13 || memcmp(blob.data(), "XGL1", 4) != 0) {
    fprintf(stderr, "FAIL magic\n");
    return 1;
  }
  size_t off = 4;
  uint32_t payload_len = ReadU32BE(blob.data() + off);
  off += 4;
  if (off + payload_len + 4 > blob.size()) { fprintf(stderr, "FAIL payload len\n"); return 1; }
  const uint8_t* payload = blob.data() + off;
  off += payload_len;
  uint32_t sig_len = ReadU32BE(blob.data() + off);
  off += 4;
  if (off + sig_len != blob.size()) { fprintf(stderr, "FAIL sig len\n"); return 1; }

  uint8_t digest[32];
  Sha256Buf(payload, payload_len, digest);
  if (!RsaVerifyWithModulus(blob.data() + off, sig_len, digest, modulus.data())) {
    fprintf(stderr, "FAIL rsa verify\n");
    return 1;
  }

  LicensePayload parsed;
  if (!ParsePayload(std::string(reinterpret_cast<const char*>(payload), payload_len), &parsed)) {
    fprintf(stderr, "FAIL payload parse\n");
    return 1;
  }
  if (parsed.hwid != argv[3]) {
    fprintf(stderr, "FAIL hwid mismatch: %s != %s\n", parsed.hwid.c_str(), argv[3]);
    return 1;
  }

  // Flipping any payload byte must break the signature.
  std::vector<uint8_t> tampered(payload, payload + payload_len);
  tampered[payload_len / 2] ^= 0x01;
  uint8_t tampered_digest[32];
  Sha256Buf(tampered.data(), tampered.size(), tampered_digest);
  if (RsaVerifyWithModulus(blob.data() + off, sig_len, tampered_digest, modulus.data())) {
    fprintf(stderr, "FAIL tampered payload accepted\n");
    return 1;
  }

  printf("OK patreon=%s hwid=%s slots=%s/%s\n", parsed.patreon_id.c_str(),
         parsed.hwid.c_str(), parsed.slots_used.c_str(), parsed.max_slots.c_str());
  return 0;
}
#endif  // XANITE_LICENSE_SELFTEST

// ---------------------------------------------------------------------------
// JNI surface
// ---------------------------------------------------------------------------

extern "C" {

JNIEXPORT jint JNICALL
Java_Ali_Xanite_LicenseNative_nativeVerifyLicense(JNIEnv* env, jclass, jobject context) {
  LicensePayload payload;
  return VerifyLicense(env, context, &payload);
}

JNIEXPORT jstring JNICALL
Java_Ali_Xanite_LicenseNative_nativeDeriveHwid(JNIEnv* env, jclass, jobject context) {
  std::string hwid = DeriveHwid(env, context);
  if (hwid.empty()) return nullptr;
  return env->NewStringUTF(hwid.c_str());
}

// "patreonId\nslotsUsed\nmaxSlots\ncreatedMs" for the settings screen, or null
// when there is no valid license. Reads from the signed payload only, so the
// UI cannot be made to show an activated state without a real license.
JNIEXPORT jstring JNICALL
Java_Ali_Xanite_LicenseNative_nativeGetLicenseInfo(JNIEnv* env, jclass, jobject context) {
  LicensePayload payload;
  if (VerifyLicense(env, context, &payload) != LICENSE_VALID) return nullptr;
  std::string out = payload.patreon_id + "\n" + payload.slots_used + "\n" +
                    payload.max_slots + "\n" + payload.created_ms;
  return env->NewStringUTF(out.c_str());
}

// Hard tamper check. Release builds abort when the APK was re-signed; debug
// builds compile the check out entirely so day-to-day development still runs.
JNIEXPORT void JNICALL
Java_Ali_Xanite_LicenseNative_nativeEnforceApkSignature(JNIEnv* env, jclass, jobject context) {
#ifdef XANITE_ENFORCE_APK_SIGNATURE
  std::string actual = GetSigningCertSha256(env, context);
  if (actual.empty()) {
    LOGW("signing certificate unavailable");
    _exit(0);
  }
  std::string expected(kExpectedCertSha256);
  if (actual.size() != expected.size()) _exit(0);
  uint8_t diff = 0;
  for (size_t i = 0; i < actual.size(); i++) {
    diff = uint8_t(diff | (uint8_t(actual[i]) ^ uint8_t(expected[i])));
  }
  if (diff != 0) {
    LOGW("signing certificate mismatch");
    _exit(0);
  }
#else
  (void)env;
  (void)context;
#endif
}

}  // extern "C"
