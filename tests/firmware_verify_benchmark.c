/* Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Timing benchmark for verifying a firmware image.
 */

#include <stdio.h>
#include <stdlib.h>

#include "file_keys.h"
#include "firmware_image.h"
#include "padding.h"
#include "rsa_utility.h"
#include "timer_utils.h"
#include "utility.h"

#define FILE_NAME_SIZE 128
#define NUM_OPERATIONS 100  /* Number of verify operations to time. */
#define FIRMWARE_SIZE_SMALL 512000
#define FIRMWARE_SIZE_MEDIUM 1024000
#define FIRMWARE_SIZE_LARGE 4096000

uint8_t* GenerateTestFirmwareBlob(int algorithm,
                                  int firmware_len,
                                  const uint8_t* firmware_sign_key,
                                  const char* root_key_file,
                                  const char* firmware_sign_key_file) {
  FirmwareImage* image = FirmwareImageNew();
  uint8_t* firmware_blob = NULL;
  uint64_t firmware_blob_len = 0;

  Memcpy(image->magic, FIRMWARE_MAGIC, FIRMWARE_MAGIC_SIZE);
  image->firmware_sign_algorithm = algorithm;
  image->firmware_sign_key = (uint8_t*) Malloc(
      RSAProcessedKeySize(image->firmware_sign_algorithm));
  Memcpy(image->firmware_sign_key, firmware_sign_key,
         RSAProcessedKeySize(image->firmware_sign_algorithm));
  image->firmware_key_version = 1;

  /* Update correct header length. */
  image->header_len = GetFirmwareHeaderLen(image);

  /* Calculate SHA-512 digest on header and populate header_checksum. */
  CalculateFirmwareHeaderChecksum(image, image->header_checksum);

  /* Populate firmware and preamble with dummy data. */
  image->firmware_version = 1;
  image->firmware_len = firmware_len;
  image->preamble_signature = image->firmware_signature = NULL;
  Memset(image->preamble, 'P', FIRMWARE_PREAMBLE_SIZE);
  image->firmware_data = Malloc(image->firmware_len);
  Memset(image->firmware_data, 'F', image->firmware_len);

  if (!AddFirmwareKeySignature(image, root_key_file)) {
    fprintf(stderr, "Couldn't create key signature.\n");
    FirmwareImageFree(image);
    return NULL;
  }

  if (!AddFirmwareSignature(image, firmware_sign_key_file)) {
    fprintf(stderr, "Couldn't create firmware and preamble signature.\n");
    FirmwareImageFree(image);
    return NULL;
  }
  firmware_blob = GetFirmwareBlob(image, &firmware_blob_len);
  FirmwareImageFree(image);
  return firmware_blob;
}

int SpeedTestAlgorithm(int algorithm) {
  int i, key_size, error_code = 0;
  ClockTimerState ct;
  double msecs;
  uint64_t len;
  uint8_t* firmware_sign_key = NULL;
  uint8_t* root_key_blob = NULL;
  char firmware_sign_key_file[FILE_NAME_SIZE];
  char file_name[FILE_NAME_SIZE];
  char* sha_strings[] = {  /* Maps algorithm->SHA algorithm. */
    "sha1", "sha256", "sha512",  /* RSA-1024 */
    "sha1", "sha256", "sha512",  /* RSA-2048 */
    "sha1", "sha256", "sha512",  /* RSA-4096 */
    "sha1", "sha256", "sha512",  /* RSA-8192 */
  };
  /* Test for three different firmware sizes. */
  uint8_t* firmware_blob_small = NULL;
  uint8_t* firmware_blob_medium = NULL;
  uint8_t* firmware_blob_large = NULL;

  key_size = siglen_map[algorithm] * 8;  /* in bits. */
  snprintf(firmware_sign_key_file, FILE_NAME_SIZE, "testkeys/key_rsa%d.pem",
           key_size);

  snprintf(file_name, FILE_NAME_SIZE, "testkeys/key_rsa%d.keyb", key_size);
  firmware_sign_key = BufferFromFile(file_name, &len);
  if (!firmware_sign_key) {
    fprintf(stderr, "Couldn't read pre-processed firmware signing key.\n");
    error_code = 1;
    goto cleanup;
  }

  /* Generate test images. */
  firmware_blob_small = GenerateTestFirmwareBlob(algorithm,
                                                 FIRMWARE_SIZE_SMALL,
                                                 firmware_sign_key,
                                                 "testkeys/key_rsa8192.pem",
                                                 firmware_sign_key_file);
  firmware_blob_medium = GenerateTestFirmwareBlob(algorithm,
                                                  FIRMWARE_SIZE_MEDIUM,
                                                  firmware_sign_key,
                                                  "testkeys/key_rsa8192.pem",
                                                  firmware_sign_key_file);
  firmware_blob_large = GenerateTestFirmwareBlob(algorithm,
                                                 FIRMWARE_SIZE_LARGE,
                                                 firmware_sign_key,
                                                 "testkeys/key_rsa8192.pem",
                                                 firmware_sign_key_file);
  if (!firmware_blob_small || !firmware_blob_medium || !firmware_blob_large) {
    fprintf(stderr, "Couldn't generate test firmware images.\n");
    error_code = 1;
    goto cleanup;
  }

  root_key_blob = BufferFromFile("testkeys/key_rsa8192.keyb", &len);
  if (!root_key_blob) {
    fprintf(stderr, "Couldn't read pre-processed rootkey.\n");
    error_code = 1;
    goto cleanup;
  }

  /* Small.*/
  StartTimer(&ct);
  for (i = 0; i < NUM_OPERATIONS; ++i) {
    if (VERIFY_FIRMWARE_SUCCESS !=
        VerifyFirmware(root_key_blob, firmware_blob_small, 0))
      fprintf(stderr, "Warning: Firmware Verification Failed.\n");
  }
  StopTimer(&ct);
  msecs = (float) GetDurationMsecs(&ct) / NUM_OPERATIONS;
  fprintf(stderr,
          "# Firmware (Small, Algo = %s):"
          "\t%.02f ms/verification\n",
          algo_strings[algorithm], msecs);
  fprintf(stdout, "ms_firmware_sm_rsa%d_%s:%.02f\n",
          key_size,
          sha_strings[algorithm],
          msecs);

  /* Medium. */
  StartTimer(&ct);
  for (i = 0; i < NUM_OPERATIONS; ++i) {
    if (VERIFY_FIRMWARE_SUCCESS !=
        VerifyFirmware(root_key_blob, firmware_blob_medium, 0))
      fprintf(stderr, "Warning: Firmware Verification Failed.\n");
  }
  StopTimer(&ct);
  msecs = (float) GetDurationMsecs(&ct) / NUM_OPERATIONS;
  fprintf(stderr,
          "# Firmware (Medium, Algo = %s):"
          "\t%.02f ms/verification\n",
          algo_strings[algorithm], msecs);
  fprintf(stdout, "ms_firmware_med_rsa%d_%s:%.02f\n",
          key_size,
          sha_strings[algorithm],
          msecs);


  /* Large */
  StartTimer(&ct);
  for (i = 0; i < NUM_OPERATIONS; ++i) {
    if (VERIFY_FIRMWARE_SUCCESS !=
        VerifyFirmware(root_key_blob, firmware_blob_large, 0))
      fprintf(stderr, "Warning: Firmware Verification Failed.\n");
  }
  StopTimer(&ct);
  msecs = (float) GetDurationMsecs(&ct) / NUM_OPERATIONS;
  fprintf(stderr,
          "# Firmware (Large, Algorithm = %s):"
          "\t%.02f ms/verification\n",
          algo_strings[algorithm], msecs);
  fprintf(stdout, "ms_firmware_large_rsa%d_%s:%.02f\n",
          key_size,
          sha_strings[algorithm],
          msecs);

 cleanup:
  Free(root_key_blob);
  Free(firmware_blob_large);
  Free(firmware_blob_medium);
  Free(firmware_blob_small);
  return error_code;
}


int main(int argc, char* argv[]) {
  int i, error_code = 0;
  for (i = 0; i < kNumAlgorithms; ++i) {
    if (0 != (error_code = SpeedTestAlgorithm(i)))
      return error_code;
  }
  return 0;
}
