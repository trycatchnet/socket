#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

/*
* It generates a 12-byte transaction ID for the STUN request.
*/
void generate_transaction_id(uint8_t tid[12]);

#endif // !CRYPTO_H
