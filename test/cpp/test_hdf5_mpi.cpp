/**
 * @file test_hdf5_mpi.cpp
 * @brief MPI + HDF5 smoke test for dftracer interception (C++)
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <hdf5.h>
#include <mpi.h>

#include <cstdio>
#include <cstring>
#include <vector>

static int write_rank_file(const char* file_path, int rank, int size) {
  hid_t file = H5Fcreate(file_path, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (file < 0) {
    std::fprintf(stderr, "Rank %d: H5Fcreate failed\n", rank);
    return 1;
  }

  hsize_t dims[1] = {16};
  hid_t space = H5Screate_simple(1, dims, nullptr);
  if (space < 0) {
    H5Fclose(file);
    std::fprintf(stderr, "Rank %d: H5Screate_simple failed\n", rank);
    return 2;
  }

  hid_t dset = H5Dcreate2(file, "rank_data_cpp", H5T_NATIVE_INT, space,
                          H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dset < 0) {
    H5Sclose(space);
    H5Fclose(file);
    std::fprintf(stderr, "Rank %d: H5Dcreate2 failed\n", rank);
    return 3;
  }

  std::vector<int> wbuf(16);
  std::vector<int> rbuf(16, -1);
  for (int i = 0; i < 16; i++) {
    wbuf[i] = rank * 1000 + i + size;
  }

  if (H5Dwrite(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
               wbuf.data()) < 0) {
    H5Dclose(dset);
    H5Sclose(space);
    H5Fclose(file);
    std::fprintf(stderr, "Rank %d: H5Dwrite failed\n", rank);
    return 4;
  }

  if (H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
              rbuf.data()) < 0) {
    H5Dclose(dset);
    H5Sclose(space);
    H5Fclose(file);
    std::fprintf(stderr, "Rank %d: H5Dread failed\n", rank);
    return 5;
  }

  for (int i = 0; i < 16; i++) {
    if (wbuf[i] != rbuf[i]) {
      H5Dclose(dset);
      H5Sclose(space);
      H5Fclose(file);
      std::fprintf(stderr, "Rank %d: data validation failed at %d\n", rank, i);
      return 6;
    }
  }

  H5Dclose(dset);
  H5Sclose(space);
  H5Fclose(file);
  return 0;
}

int main(int argc, char* argv[]) {
  int rank = 0;
  int size = 0;
  int provided = 0;
  bool init_dftracer = false;

  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (argc > 2 && std::strcmp(argv[2], "1") == 0) {
    DFTRACER_CPP_INIT(nullptr, nullptr, nullptr);
    init_dftracer = true;
  }

  const char* data_dir = (argc > 1) ? argv[1] : "/tmp";
  char filename[1024];
  std::snprintf(filename, sizeof(filename), "%s/test_hdf5_mpi_cpp_rank_%d.h5",
                data_dir, rank);

  DFTRACER_CPP_METADATA(meta, "hdf5_mpi_test", "hdf5_mpi_smoke_cpp");

  MPI_Barrier(MPI_COMM_WORLD);

  int rc = write_rank_file(filename, rank, size);

  int local = rank;
  int sum = 0;
  MPI_Allreduce(&local, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  MPI_Barrier(MPI_COMM_WORLD);

  if (init_dftracer) {
    DFTRACER_CPP_FINI();
  }

  MPI_Finalize();

  if (rank == 0 && rc == 0) {
    std::printf("HDF5+MPI C++ smoke test completed successfully (sum=%d)\n",
                sum);
  }
  return rc;
}
