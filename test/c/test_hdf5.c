/**
 * @file test_hdf5.c
 * @brief Basic HDF5 smoke test for dftracer HDF5 interception
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <hdf5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int create_and_validate_dataset(const char* file_path) {
  hid_t file = H5Fcreate(file_path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (file < 0) {
    fprintf(stderr, "H5Fcreate failed: %s\n", file_path);
    return 1;
  }

  hsize_t dims[1] = {16};
  hid_t space = H5Screate_simple(1, dims, NULL);
  if (space < 0) {
    H5Fclose(file);
    fprintf(stderr, "H5Screate_simple failed\n");
    return 2;
  }

  hid_t dset = H5Dcreate2(file, "dset", H5T_NATIVE_INT, space, H5P_DEFAULT,
                          H5P_DEFAULT, H5P_DEFAULT);
  if (dset < 0) {
    H5Sclose(space);
    H5Fclose(file);
    fprintf(stderr, "H5Dcreate2 failed\n");
    return 3;
  }

  int wbuf[16];
  int rbuf[16];
  for (int i = 0; i < 16; i++) {
    wbuf[i] = i * 3;
    rbuf[i] = -1;
  }

  if (H5Dwrite(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, wbuf) < 0) {
    H5Dclose(dset);
    H5Sclose(space);
    H5Fclose(file);
    fprintf(stderr, "H5Dwrite failed\n");
    return 4;
  }

  if (H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, rbuf) < 0) {
    H5Dclose(dset);
    H5Sclose(space);
    H5Fclose(file);
    fprintf(stderr, "H5Dread failed\n");
    return 5;
  }

  for (int i = 0; i < 16; i++) {
    if (wbuf[i] != rbuf[i]) {
      H5Dclose(dset);
      H5Sclose(space);
      H5Fclose(file);
      fprintf(stderr, "HDF5 data validation failed at index %d\n", i);
      return 6;
    }
  }

  H5Dclose(dset);
  H5Sclose(space);
  H5Fclose(file);
  return 0;
}

int main(int argc, char* argv[]) {
  int init_dftracer = 0;
  if (argc > 2 && strcmp(argv[2], "1") == 0) {
    DFTRACER_C_INIT(NULL, NULL, NULL);
    init_dftracer = 1;
  }

  const char* data_dir = (argc > 1) ? argv[1] : "/tmp";
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/test_hdf5_smoke.h5", data_dir);

  DFTRACER_C_METADATA(meta, "hdf5_test", "hdf5_smoke_test");

  int rc = create_and_validate_dataset(filename);

  if (init_dftracer) {
    DFTRACER_C_FINI();
  }

  if (rc == 0) {
    printf("HDF5 C smoke test completed successfully\n");
  }
  return rc;
}
