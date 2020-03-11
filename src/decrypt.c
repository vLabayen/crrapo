/* Standard library */
#include <stdlib.h>
#include <string.h>

/* OpenSSL */
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/err.h>

/* Internal modules */
#include "czarrapo.h"
#include "utils.h"
#include "error_handling.h"

/*
 * Opens a private key file in PEM format with a passphrase and returns an allocated
 * RSA struct.
 */
static RSA* _read_private_key(const char* private_key_file, const char* passphrase) {
	RSA* rsa;
	FILE *pk;

	/* Allocate RSA struct */
	if ( (rsa = RSA_new()) == NULL ){
		return NULL;
	}

	/* Read private key from file, assign to RSA struct and close file */
	if ( (pk = fopen(private_key_file, "r")) == NULL ) {
		RSA_free(rsa);
		return NULL;
	}
	if ( (rsa = PEM_read_RSAPrivateKey(pk, &rsa, NULL, (void*) passphrase)) == NULL ) {
		fclose(pk);
		RSA_free(rsa);
		return NULL;
	}
	DEBUG_PRINT(("[DEBUG] Private key file at %s read correctly.\n", private_key_file));

	fclose(pk);
	return rsa;
}

/*
 * Reads header values that are always present ('fast' flag and the challenge).
 */
void _read_basic_header(FILE* encrypted_file_handle, bool* fast, unsigned char* challenge, RSA* rsa) {
	int amount_read;

	if ( (amount_read = fread(fast, sizeof(bool), 1, encrypted_file_handle)) < sizeof(bool) ) {
		_handle_RSA_error("[ERROR] Could not read 'fast' flag from encrypted file header.\n", false, rsa, NULL);
		_handle_file_action_error("", true, encrypted_file_handle);
	}
	DEBUG_PRINT(("[DEBUG] ++ HEADER: Fast mode flag read (%i bytes).\n", amount_read));

	if ( (amount_read = fread(challenge, sizeof(unsigned char), _CHALLENGE_SIZE, encrypted_file_handle)) < (sizeof(unsigned char) * _CHALLENGE_SIZE) ) {
		_handle_RSA_error("[ERROR] Could not read challenge from encrypted file header.\n", false, rsa, NULL);
		_handle_file_action_error("", true, encrypted_file_handle);
	}
	DEBUG_PRINT(("[DEBUG] ++ HEADER: Challenge read (%i bytes).\n", amount_read));
}

/*
 * For each block in 'fp' of size 'rsa_block_size':
 * 1. Decrypt with RSA private key
 * 2. _CHALLENGE_HASH(_BLOCK_HASH(decrypted block + password)) and compare with read challenge
 */
static size_t __find_block_slow(FILE* fp, size_t file_size, unsigned int block_size, RSA* rsa, unsigned int rsa_block_size, char* password, unsigned char* challenge) {
 	unsigned char rsa_block[rsa_block_size];	/* Buffer to store each read block */
 	long int end_of_header = ftell(fp);		/* Index to the first byte of actual encrypted data*/
	size_t i;					/* Current index in encrypted file */
	int amount_read, padding;			/* Bytes returned from fread(), padding type for decryption */

	int decrypted_block_len;					/* Decrypted block length returned from RSA_private_decrypt() */
	unsigned char decrypted_block[block_size + strlen(password)];	/* Buffer to store decrypted block and later concatenate password */
	unsigned char block_hash[_BLOCK_HASH_SIZE];			/* Buffer to store hash of decrypted block */
	unsigned char new_challenge[_CHALLENGE_SIZE];			/* Buffer to store challenge */

	/* Determine padding */
	if (block_size == rsa_block_size) {
		padding = RSA_NO_PADDING;
		DEBUG_PRINT(("[DEBUG] Using no padding for RSA decryption.\n"));
	} else {
		padding = RSA_PKCS1_OAEP_PADDING;
		DEBUG_PRINT(("[DEBUG] Using OAEP padding for RSA decryption.\n"));
	}


 	/*
 	 * Read blocks of size 'rsa_block_size' in jumps of 'block_size'.
 	 * For simplicity, take end_of_header = 0 (beginning of file), block_size = 256 and
 	 * rsa_block_size = 512 (4096 bit key).
 	 * i=0: 0----------------512
 	 * i=1: |       256---------------768
 	 * i=2: |                512---------------1024
 	 */
 	for (i=0; i<file_size; i+=block_size) {
 		fseek(fp, end_of_header + i, SEEK_SET);
 		amount_read = fread(rsa_block, sizeof(unsigned char), rsa_block_size, fp);

 		/* Decrypt block */
 		if ( (decrypted_block_len = RSA_private_decrypt(amount_read, rsa_block, decrypted_block, rsa, padding)) < 0 ) {
 			continue;
 		}

 		/* Concatenate with password and get block hash*/
 		memcpy(&decrypted_block[decrypted_block_len], password, strlen(password));
 		_hash_individual_block(block_hash, decrypted_block, decrypted_block_len + strlen(password), _BLOCK_HASH);

 		/* Get challenge */
 		_hash_individual_block(new_challenge, block_hash, _BLOCK_HASH_SIZE, _CHALLENGE_HASH);

 		/* Compare with challenge read from header */
 		if (memcmp(new_challenge, challenge, _CHALLENGE_SIZE) == 0) {
 			return (i/block_size);
 		}
 	}

 	_handle_simple_error("[ERROR] RSA block could not be found.\n");
}

static size_t __find_block_fast(FILE* fp, size_t file_size, unsigned int block_size, RSA* rsa, unsigned int rsa_block_size, char* password, unsigned char* challenge, unsigned char* auth) {
	
	// TODO: _AUTH_HASH(challenge + block_index + password) and compare with known auth
	// Can probably memcpy over block_index in each iteration and save copying the challenge
	// and the password over and over in the input buffer for the hash.

	return 0;
}

/*
 * Finds the selected block in the encrypted file. This function reads the file header on its own.
 * Due to the way RSA decryption works, the struct 'rsa' will be freed inside this function.
 */
static size_t _find_selected_block(char* encrypted_file, size_t encrypted_file_size, unsigned int block_size, RSA* rsa, unsigned int rsa_block_size, char* password) {
	FILE* fp;					/* File handle to read input file */
	bool fast;					/* Fast flag read from file */
	unsigned char challenge[_CHALLENGE_SIZE];	/* Challenge read from file */
	size_t selected_block_index;			/* Output variable */

	/* Open file handle to read with */
	if ( (fp = fopen(encrypted_file, "rb")) == NULL ) {
		_handle_RSA_error("[ERROR] Could not open the encrypted file.\n", true, rsa, NULL);
	}

	/* Read first part of header */
	DEBUG_PRINT(("[DEBUG] Reading file header.\n"));
	_read_basic_header(fp, &fast, challenge, rsa);

	/* Read second part of header if fast mode */
	if (fast) {
		unsigned char auth[_AUTH_SIZE];
		if ( fread(auth, sizeof(unsigned char), _AUTH_SIZE, fp) < ((sizeof(unsigned char)) * _AUTH_SIZE) ) {
			_handle_RSA_error("[ERROR] Could not read auth from encrypted file header. Make sure the 'fast' flag is properly set.\n", false, rsa, NULL);
			_handle_file_action_error("", true, fp);
		}
		DEBUG_PRINT(("[DEBUG] ++ HEADER: Auth read (%i bytes).\n", _AUTH_SIZE));
		selected_block_index = __find_block_fast(fp, encrypted_file_size, block_size, rsa, rsa_block_size, password, challenge, auth);
	} else {
		selected_block_index = __find_block_slow(fp, encrypted_file_size, block_size, rsa, rsa_block_size, password, challenge);
	}

	fclose(fp);
	return selected_block_index;
}

void decrypt_file(char* encrypted_file, char* decrypted_file, unsigned int block_size, char* password, char* private_key_file, char* passphrase, long int passed_block_index) {
	RSA* rsa;							/* RSA struct */
	unsigned int rsa_block_size;					/* Size of RSA blocks, based on key size */
	size_t file_size, num_rsa_blocks;				/* Variables on input file */
	size_t selected_block_index;					/* Block to be used for symmetric decryption. This will be 'passed_block_index' if >= 0*/
	size_t encrypted_file_size = _get_file_size(encrypted_file);	/* Size of input file */

	/* Check that we have a valid block size */
	if (!_ispowerof2(block_size)) {
		char err_msg[ERR_MSG_BUF_SIZE];
		snprintf(err_msg, ERR_MSG_BUF_SIZE, "[ERROR] block_size %i must be a power of 2.\n", block_size);
		_handle_simple_error(err_msg);
	}

	/* Get RSA output block size for current key */
	if ( (rsa = _read_private_key(private_key_file, passphrase)) == NULL ){
		_handle_simple_error("[ERROR] Could not open private key file.\n");
	}
	rsa_block_size = RSA_size(rsa);

	/* Find RSA block */
	if ( passed_block_index < 0 ) {
		selected_block_index = _find_selected_block(encrypted_file, encrypted_file_size, block_size, rsa, rsa_block_size, password);
	} else {
		selected_block_index = passed_block_index;
	}
	RSA_free(rsa);

	// This is always zero for now
	DEBUG_PRINT(("[DEBUG] Found decryption block (index: %lu).\n", selected_block_index));

	// TODO: decrypt file with found block

}
