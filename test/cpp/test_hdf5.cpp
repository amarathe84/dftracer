/**
 * @file test_hdf5.cpp
 * @brief Basic HDF5 smoke test for dftracer HDF5 interception (C++)
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <hdf5.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int create_and_validate_dataset(const char* file_path) {
  hid_t file = H5Fcreate(file_path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (file < 0) {
    std::fprintf(stderr, "H5Fcreate failed: %s\n", file_path);
    return 1;
  }

  hsize_t dims[1] = {32};
  hid_t space = H5Screate_simple(1, dims, nullptr);
  if (space < 0) {
    H5Fclose(file);
    std::fprintf(stderr, "H5Screate_simple failed\n");
    return 2;
  }

  hid_t dset = H5Dcreate2(file, "dset", H5T_NATIVE_INT, space, H5P_DEFAULT,
                          H5P_DEFAULT, H5P_DEFAULT);
  if (dset < 0) {
    H5Sclose(space);
    H5Fclose(file);
    std::fprintf(stderr, "H5Dcreate2 failed\n");
    return 3;
  }

  std::vector<int> wbuf(32);
  std::vector<int> rbuf(32, -1);

  for (int i = 0; i < 32; i++) {
    wbuf[i] = i * 5;
  }

  if (H5Dwrite(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
               wbuf.data()) < 0) {
    H5Dclose(dset);
    H5Sclose(space);
    H5Fclose(file);
    std::fprintf(stderr, "H5Dwrite failed\n");
    return 4;
  }

  if (H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
              rbuf.data()) < 0) {
    H5Dclose(dset);
    H5Sclose(space);
    H5Fclose(file);
    std::fprintf(stderr, "H5Dread failed\n");
    return 5;
  }

  for (int i = 0; i < 32; i++) {
    if (wbuf[i] != rbuf[i]) {
      H5Dclose(dset);
      H5Sclose(space);
      H5Fclose(file);
      std::fprintf(stderr, "HDF5 data validation failed at index %d\n", i);
      return 6;
    }
  }

  H5Dclose(dset);
  H5Sclose(space);
  H5Fclose(file);
  return 0;
}

int main(int argc, char* argv[]) {
  bool init_dftracer = false;
  if (argc > 2 && std::strcmp(argv[2], "1") == 0) {
    DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
    init_dftracer = true;
  }

  const char* data_dir = (argc > 1) ? argv[1] : "/tmp";
  char filename[1024];
  std::snprintf(filename, sizeof(filename), "%s/test_hdf5_smoke_cpp.h5", data_dir);

  DFTRACER_CPP_METADATA(meta, "hdf5_test", "hdf5_smoke_test_cpp");

  int rc = create_and_validate_dataset(filename);

  if (init_dftracer) {
    DFTRACER_CPP_FINI();
  }

  if (rc == 0) {
    std::printf("HDF5 C++ smoke test completed successfully\n");
  }
  return rc;
}
