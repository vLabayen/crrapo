#include <stdbool.h>
#include "error_handling.h"

void _handle_simple_error(const char* msg) {
	printf("%s", msg);
	exit(1);
}

void _handle_file_action_error(const char* msg, bool exit_on_handle, FILE* fp) {
	printf("%s", msg);
	if (fp != NULL) {
		fclose(fp);
	}
	if (exit_on_handle) {
		exit(1);
	}
}

void _handle_RSA_error(const char* msg, bool exit_on_handle, RSA* rsa, BIGNUM* e) {
	printf("%s", msg);
	RSA_free(rsa);
	BN_clear_free(e);
	if (exit_on_handle) {
		exit(1);
	}
}

/* Function to be called to handle errors with EVP hashing. */
void _handle_EVP_MD_error(const char* msg, bool exit_on_handle, EVP_MD_CTX* evp_ctx) {
	printf("%s", msg);
	if (evp_ctx != NULL) {
		EVP_MD_CTX_free(evp_ctx);
	}
	if (exit_on_handle) {
		exit(1);
	}
}

/* Function to be called to handle errors with EVP encryption. */
void _handle_EVP_CIPHER_error(const char* msg, bool exit_on_handle, EVP_CIPHER_CTX* evp_ctx, FILE* f1, FILE* f2) {
	printf("%s", msg);
	EVP_CIPHER_CTX_free(evp_ctx);
	if (f1 != NULL) {
		fclose(f1);
	}
	if (f2 != NULL) {
		fclose(f2);
	}
	if (exit_on_handle) {
		exit(1);
	}
}
