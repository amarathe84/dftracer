/**
 * @file test_hdf5_parallel.c
 * @brief Parallel HDF5 + MPI smoke test for dftracer interception
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <hdf5.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_parallel_hdf5(const char* file_path, int rank, int nprocs) {
#if defined(H5_HAVE_PARALLEL)
  const hsize_t local_elems = 8;
  const hsize_t global_elems = local_elems * (hsize_t)nprocs;

  hid_t fapl = H5Pcreate(H5P_FILE_ACCESS);
  if (fapl < 0) return 10;
  if (H5Pset_fapl_mpio(fapl, MPI_COMM_WORLD, MPI_INFO_NULL) < 0) {
    H5Pclose(fapl);
    return 11;
  }

  hid_t file = H5Fcreate(file_path, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
  H5Pclose(fapl);
  if (file < 0) return 12;

  hsize_t dims[1] = {global_elems};
  hid_t filespace = H5Screate_simple(1, dims, NULL);
  if (filespace < 0) {
    H5Fclose(file);
    return 13;
  }

  hid_t dset = H5Dcreate2(file, "parallel_dset", H5T_NATIVE_INT, filespace,
                          H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dset < 0) {
    H5Sclose(filespace);
    H5Fclose(file);
    return 14;
  }

  hsize_t memdims[1] = {local_elems};
  hid_t memspace = H5Screate_simple(1, memdims, NULL);
  if (memspace < 0) {
    H5Dclose(dset);
    H5Sclose(filespace);
    H5Fclose(file);
    return 15;
  }

  hsize_t start[1] = {(hsize_t)rank * local_elems};
  hsize_t count[1] = {local_elems};
  if (H5Sselect_hyperslab(filespace, H5S_SELECT_SET, start, NULL, count, NULL) <
      0) {
    H5Sclose(memspace);
    H5Dclose(dset);
    H5Sclose(filespace);
    H5Fclose(file);
    return 16;
  }

  hid_t dxpl = H5Pcreate(H5P_DATASET_XFER);
  if (dxpl < 0) {
    H5Sclose(memspace);
    H5Dclose(dset);
    H5Sclose(filespace);
    H5Fclose(file);
    return 17;
  }
  if (H5Pset_dxpl_mpio(dxpl, H5FD_MPIO_COLLECTIVE) < 0) {
    H5Pclose(dxpl);
    H5Sclose(memspace);
    H5Dclose(dset);
    H5Sclose(filespace);
    H5Fclose(file);
    return 18;
  }

  int wbuf[8];
  int rbuf[8];
  for (int i = 0; i < 8; ++i) {
    wbuf[i] = rank * 100 + i;
    rbuf[i] = -1;
  }

  if (H5Dwrite(dset, H5T_NATIVE_INT, memspace, filespace, dxpl, wbuf) < 0) {
    H5Pclose(dxpl);
    H5Sclose(memspace);
    H5Dclose(dset);
    H5Sclose(filespace);
    H5Fclose(file);
    return 19;
  }

  if (H5Dread(dset, H5T_NATIVE_INT, memspace, filespace, dxpl, rbuf) < 0) {
    H5Pclose(dxpl);
    H5Sclose(memspace);
    H5Dclose(dset);
    H5Sclose(filespace);
    H5Fclose(file);
    return 20;
  }

  for (int i = 0; i < 8; ++i) {
    if (wbuf[i] != rbuf[i]) {
      H5Pclose(dxpl);
      H5Sclose(memspace);
      H5Dclose(dset);
      H5Sclose(filespace);
      H5Fclose(file);
      return 21;
    }
  }

  H5Pclose(dxpl);
  H5Sclose(memspace);
  H5Dclose(dset);
  H5Sclose(filespace);
  H5Fclose(file);
  return 0;
#else
  (void)file_path;
  (void)rank;
  (void)nprocs;
  return 0;
#endif
}

int main(int argc, char* argv[]) {
  int rank = 0;
  int size = 0;
  int provided = 0;
  int init_dftracer = 0;

  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  if (argc > 2 && strcmp(argv[2], "1") == 0) {
    DFTRACER_C_INIT(NULL, NULL, NULL);
    init_dftracer = 1;
  }

  const char* data_dir = (argc > 1) ? argv[1] : "/tmp";
  char filename[1024];
  snprintf(filename, sizeof(filename), "%s/test_hdf5_parallel.h5", data_dir);

  DFTRACER_C_METADATA(meta, "hdf5_parallel_test", "hdf5_parallel_smoke_c");

  MPI_Barrier(MPI_COMM_WORLD);

  int rc = run_parallel_hdf5(filename, rank, size);

  int local = rank;
  int total = 0;
  MPI_Allreduce(&local, &total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  MPI_Barrier(MPI_COMM_WORLD);

#if !defined(H5_HAVE_PARALLEL)
  if (rank == 0) {
    printf(
        "Parallel HDF5 unavailable (H5_HAVE_PARALLEL undefined), skipping "
        "parallel HDF5 calls\n");
  }
#endif

  if (init_dftracer) {
    DFTRACER_C_FINI();
  }

  MPI_Finalize();

  if (rank == 0 && rc == 0) {
    printf("HDF5 parallel C smoke test completed successfully (sum=%d)\n",
           total);
  }

  return rc;
}
