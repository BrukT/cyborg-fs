#include "crypto.h"

Crypto::Crypto(const unsigned char* session_key, const unsigned char* auth_key, const unsigned char * iv) : auth_key(auth_key) {
    //create and initialize context for encryption and decryption
	ctx_e = EVP_CIPHER_CTX_new();
	EVP_EncryptInit(ctx_e, EVP_aes_128_cfb8(), session_key, iv);

    ctx_d = EVP_CIPHER_CTX_new();
    EVP_DecryptInit(ctx_d, EVP_aes_128_cfb8(), session_key, iv);

    this->auth_key = auth_key;

    /* this->auth_key = new unsigned char[HMAC_LEN];
    memcpy(this->auth_key, auth_key, HMAC_LEN); */

    debug(DEBUG, "session_key + auth_key" << endl);
    hexdump(DEBUG, (const char*)session_key, AES128_KEY_LEN);
    hexdump(DEBUG, (const char*)auth_key, HMAC_LEN);
    hexdump(DEBUG, (const char*)iv, AES128_KEY_LEN);

    sequence_number_i = sequence_number_o = 0;  /* TODO -- is it ok to always initialize to 0? */

    //delete[] session_key;
    //delete[] iv;
}

int Crypto::encrypt(char* d_buffer, const char* s_buffer, int size){
	int len, r;

    /* Encrypt and update the buffer */
	if ((r = EVP_EncryptUpdate(ctx_e, (unsigned char*)d_buffer, &len, (const unsigned char*)s_buffer, size)) == 0) {
        throw ExCryptoComputation("can not EVP_EncryptUpdate");
    }

	return r;
}

int Crypto::decrypt(char* d_buffer, const char* s_buffer, int size){
	int len, r;

	/* Decrypt and update the buffer */
	if ((r = EVP_DecryptUpdate(ctx_d, (unsigned char*)d_buffer, &len, (const unsigned char*)s_buffer, size)) == 0) {
        throw ExCryptoComputation("can not EVP_DecryptUpdate");
    }

	return r;
}

int Crypto::hmac(unsigned char* digest, const Rocket& rocket) {
    unsigned int len;

    HMAC_CTX* ctx = HMAC_CTX_new();
    if (ctx == NULL) throw ExCryptoComputation("Crypto::hmac() out of memory");

    bool pass =
        HMAC_Init_ex(ctx, auth_key, HMAC_LEN, EVP_sha256(), NULL) &&
        HMAC_Update(ctx, (const unsigned char*)&rocket.length, sizeof(rocket.length)) &&
        HMAC_Update(ctx, (const unsigned char*)&rocket.sequence_number, sizeof(rocket.sequence_number)) &&
        HMAC_Final(ctx, digest, &len);

    HMAC_CTX_free(ctx);

    if (pass) return len;
    else throw ExCryptoComputation("Crypto::hmac() can not compute");
}

int Crypto::hmac(unsigned char* digest, const SpaceCraft& spacecraft, const unsigned char* encrypted_payload, int size) {
    unsigned int len;

    HMAC_CTX* ctx = HMAC_CTX_new();
    if (ctx == NULL) throw ExCryptoComputation("Crypto::hmac() out of memory");

    bool pass =
        HMAC_Init_ex(ctx, auth_key, HMAC_LEN, EVP_sha256(), NULL) &&
        HMAC_Update(ctx, (const unsigned char*)&spacecraft.sequence_number, sizeof(spacecraft.sequence_number)) &&
        HMAC_Update(ctx, encrypted_payload, size) &&
        HMAC_Final(ctx, digest, &len);

    HMAC_CTX_free(ctx);

    if (pass) return len;
    else throw ExCryptoComputation("Crypto::hmac() can not compute");
}

int Crypto::send(Connection* connection, const char* plaintext, int size) {
    SpaceCraft spacecraft;
    Rocket rocket;
    char encrypted_payload[BUFFER_SIZE];

    /* Rocket preparation */
    rocket.length = htonl(size);
    rocket.sequence_number = htonl(sequence_number_o++);
    hmac((unsigned char*)&rocket.hmac, rocket); /* computes Rocket hmac */

    /* Spacecraft preparation */
    encrypt(encrypted_payload, plaintext, size);    /* Payload encryption */
    spacecraft.sequence_number = htonl(sequence_number_o++);
    hmac((unsigned char*)&spacecraft.hmac, spacecraft, (unsigned char*)encrypted_payload, size); /* computes SpaceCraft hmac */

    /* Debug output */
    debug(DEBUG, "[D] === Crypto::send() ===" << endl);
    debug(DEBUG, "[D] PlainText:  " << endl); hexdump(DEBUG, plaintext, size);
    debug(DEBUG, "[D] Rocket:     " << endl); hexdump(DEBUG, (const char*)&rocket, sizeof(Rocket));
    debug(DEBUG, "[D] SpaceCraft: " << endl); hexdump(DEBUG, (const char*)&spacecraft, sizeof(SpaceCraft));
    debug(DEBUG, "[D] CypherText: " << endl); hexdump(DEBUG, encrypted_payload, size);

    /* TCP transmission */
    int r1, r2, r3;

    r1 = connection->send((const char*)&rocket, sizeof(Rocket));
    r2 = connection->send((const char*)&spacecraft, sizeof(SpaceCraft));
    r3 = connection->send(encrypted_payload, size);

    if (r1 >= 0 && r2 >= 0 && r3 >= 0) return r1 + r2 + r3;
    else throw ExSend("can not Crypto::send()");
}

int Crypto::recv(Connection* connection, char* buffer, int size) {
    static int remaining = 0;           /* remaining bytes in plaintext (decrypted) payload */
    static int index = 0;               /* first unread byte in plaintext (decrypted) payload */
    static char payload[BUFFER_SIZE];   /* plaintext payload */

    char encrypted_payload[BUFFER_SIZE];
    SpaceCraft spacecraft;
    Rocket rocket;

    if (remaining == 0) {
        int r;
        debug(DEBUG, "[D] Crypto::recv() -- feeding from TCP" << endl);

        /* --- Rocket --- */
        if (connection->recv((char*)&rocket, sizeof(Rocket)) != sizeof(Rocket)) throw ExRecv("can not Crypto::recv() rocket");
        debug(DEBUG, "[D] Rocket: " << endl); hexdump(DEBUG, (const char*)&rocket, sizeof(Rocket));

        /* Rocket HMAC verification */
        unsigned char* rocket_computed_hmac = new unsigned char[HMAC_LEN];
        hmac(rocket_computed_hmac, rocket);
        r = CRYPTO_memcmp(rocket_computed_hmac, (const char*)&rocket.hmac, HMAC_LEN);
        delete[] rocket_computed_hmac;
        if (r != 0) {
            debug(WARNING, "[W] rocket: bad hmac" << endl);
            throw ExBadHMAC("rocket: bad hmac");
        } else { debug(DEBUG, "[D] rocket: valid hmac" << endl); }

        rocket.length = ntohl(rocket.length);
        if (rocket.length > BUFFER_SIZE) {
            debug(WARNING, "[W] rocket: too long " << rocket.length << endl);
            throw ExBadProtocol("rocket: too long");
        }

        rocket.sequence_number = ntohl(rocket.sequence_number);
        if (rocket.sequence_number != sequence_number_i++) {
            debug(WARNING, "[W] rocket: bad sequence number " << rocket.sequence_number << endl);
            throw ExBadSeqNum("rocket: bad sequence number");
        }

        /* --- SpaceCraft --- */
        if (connection->recv((char*)&spacecraft, sizeof(SpaceCraft)) != sizeof(SpaceCraft)) throw ExRecv("can not Crypto::recv() spacecraft");
        debug(DEBUG, "[D] SpaceCraft: " << endl); hexdump(DEBUG, (const char*)&spacecraft, sizeof(SpaceCraft));
        if (connection->recv(encrypted_payload, rocket.length) != (int)rocket.length) throw ExRecv("can not Crypto::recv() encrypted_payload");
        debug(DEBUG, "[D] CypherText: " << endl); hexdump(DEBUG, encrypted_payload, rocket.length);

        /* SpaceCraft HMAC verification */
        unsigned char* spacecraft_computed_hmac = new unsigned char[HMAC_LEN];
        hmac(spacecraft_computed_hmac, spacecraft, (unsigned char*)encrypted_payload, rocket.length);
        r = CRYPTO_memcmp(spacecraft_computed_hmac, (const char*)&spacecraft.hmac, HMAC_LEN);
        delete[] spacecraft_computed_hmac;
        if (r != 0) {
            debug(WARNING, "[W] spacecraft: invalid hmac" << endl);
            throw ExBadHMAC("spacecraft: bad hmac");
        } else { debug(DEBUG, "[D] spacecraft: valid hmac" << endl); }

        /* check spacecraft sequence number */
        spacecraft.sequence_number = ntohl(spacecraft.sequence_number);
        if (spacecraft.sequence_number != sequence_number_i++) {
            debug(WARNING, "[W] spacecraft: bad sequence number " << spacecraft.sequence_number << endl);
            throw ExBadSeqNum("spacecraft: bad sequence number");
        }

        /* if everything is good, finally decrypt */
        decrypt(payload, encrypted_payload, rocket.length);

        debug(DEBUG, "[D] PlainText:  " << endl); hexdump(DEBUG, payload, rocket.length);

        remaining = rocket.length;
        index = 0;
    }

    int r = (remaining < size) ? remaining : size;
    memcpy(buffer, payload + index, r);
    index += r;
    remaining -= r;
    return r;
}

Crypto::~Crypto() {
    /* It is not needed to call EVP_*cryptFinal because no padding is inserted
        EVP_EncryptFinal(ctx_e, ...);
        EVP_DecryptFinal(ctx_d, ...);
    */

    //delete[] auth_key;

    EVP_CIPHER_CTX_free(ctx_e);
	EVP_CIPHER_CTX_free(ctx_d);
}
