#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/pem.h>  // <--- 新增這個: 解決 PEM_read/write 錯誤
#include <openssl/ec.h>   // <--- 新增這個: 解決 EC curve param 錯誤
#include <vector>
#include <iomanip>
#include <sstream>

// 處理 OpenSSL 錯誤
void handleErrors() {
    ERR_print_errors_fp(stderr);
    abort();
}

// 產生 Diffie-Hellman 參數與 Key Pair
EVP_PKEY* generate_dh_key() {
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL);
    if (!pctx) handleErrors();
    if (EVP_PKEY_paramgen_init(pctx) <= 0) handleErrors();
    if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(pctx, NID_X9_62_prime256v1) <= 0) handleErrors();
    
    EVP_PKEY* params = NULL;
    if (EVP_PKEY_paramgen(pctx, &params) <= 0) handleErrors();
    EVP_PKEY_CTX_free(pctx);

    EVP_PKEY_CTX* kctx = EVP_PKEY_CTX_new(params, NULL);
    if (!kctx) handleErrors();
    if (EVP_PKEY_keygen_init(kctx) <= 0) handleErrors();
    
    EVP_PKEY* key = NULL;
    if (EVP_PKEY_keygen(kctx, &key) <= 0) handleErrors();
    
    EVP_PKEY_CTX_free(kctx);
    EVP_PKEY_free(params);
    return key;
}

// 導出 Public Key (傳給對方用)
std::string get_public_key_pem(EVP_PKEY* key) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(bio, key);
    char* data;
    long len = BIO_get_mem_data(bio, &data);
    std::string pubKey(data, len);
    BIO_free(bio);
    return pubKey;
}

// 從 PEM 字串還原 Public Key
EVP_PKEY* load_public_key_pem(const std::string& pem) {
    BIO* bio = BIO_new_mem_buf(pem.data(), pem.size());
    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);
    return key;
}

// 計算 Shared Secret (Session Key)
unsigned char* derive_secret(EVP_PKEY* my_key, EVP_PKEY* peer_pub_key, size_t& secret_len) {
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(my_key, NULL);
    if (!ctx) handleErrors();
    if (EVP_PKEY_derive_init(ctx) <= 0) handleErrors();
    if (EVP_PKEY_derive_set_peer(ctx, peer_pub_key) <= 0) handleErrors();

    if (EVP_PKEY_derive(ctx, NULL, &secret_len) <= 0) handleErrors();
    unsigned char* secret = (unsigned char*)OPENSSL_malloc(secret_len);
    if (EVP_PKEY_derive(ctx, secret, &secret_len) <= 0) handleErrors();

    EVP_PKEY_CTX_free(ctx);
    return secret; // Remember to free this later
}

// AES 加密 (使用 Session Key)
std::string aes_encrypt(const std::string& plaintext, unsigned char* key) {
    // 簡單起見，使用 SHA256 把 DH secret 轉成 32 bytes AES Key
    unsigned char aes_key[32];
    unsigned char iv[16]; // Initialization Vector
    RAND_bytes(iv, 16);   // Random IV

    // Hash secret to get consistent key size
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    unsigned int len;
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, key, 32); // Assuming secret is at least 32 bytes
    EVP_DigestFinal_ex(mdctx, aes_key, &len);
    EVP_MD_CTX_free(mdctx);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int c_len = plaintext.size() + AES_BLOCK_SIZE, f_len = 0;
    unsigned char* ciphertext = (unsigned char*)malloc(c_len);

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, aes_key, iv);
    EVP_EncryptUpdate(ctx, ciphertext, &c_len, (unsigned char*)plaintext.c_str(), plaintext.size());
    EVP_EncryptFinal_ex(ctx, ciphertext + c_len, &f_len);

    std::string result((char*)iv, 16); // Prepend IV
    result.append((char*)ciphertext, c_len + f_len);
    
    EVP_CIPHER_CTX_free(ctx);
    free(ciphertext);
    return result;
}

// AES 解密
std::string aes_decrypt(const std::string& ciphertext_with_iv, unsigned char* key) {
    if (ciphertext_with_iv.size() < 16) return ""; // Error

    unsigned char iv[16];
    memcpy(iv, ciphertext_with_iv.data(), 16);
    std::string actual_cipher = ciphertext_with_iv.substr(16);

    unsigned char aes_key[32];
    // Same Hash logic
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    unsigned int len;
    EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(mdctx, key, 32);
    EVP_DigestFinal_ex(mdctx, aes_key, &len);
    EVP_MD_CTX_free(mdctx);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int p_len = actual_cipher.size(), f_len = 0;
    unsigned char* plaintext = (unsigned char*)malloc(p_len + AES_BLOCK_SIZE);

    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, aes_key, iv);
    if(1 != EVP_DecryptUpdate(ctx, plaintext, &p_len, (unsigned char*)actual_cipher.data(), actual_cipher.size())) {
         // handle error
    }
    if(1 != EVP_DecryptFinal_ex(ctx, plaintext + p_len, &f_len)) {
        // handle error
    }

    std::string result((char*)plaintext, p_len + f_len);
    EVP_CIPHER_CTX_free(ctx);
    free(plaintext);
    return result;
}