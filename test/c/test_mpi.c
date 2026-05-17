/**
 * @file test_mpi.c
 * @brief Basic MPI test for dftracer MPI interception
 *
 * This test exercises common MPI calls to verify that the dftracer
 * MPI brahma interception works correctly.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dftracer/dftracer.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

int main(int argc, char* argv[]) {
  int rank, size;
  int provided;
  char filename[1024];
  MPI_File fh;
  MPI_Status status;

  /* Initialize MPI */
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  /* Initialize dftracer if requested */
  int init_dftracer = 0;
  if (argc > 2) {
    if (strcmp(argv[2], "1") == 0) {
      DFTRACER_C_INIT(NULL, NULL, NULL);
      init_dftracer = 1;
    }
  }

  /* Create test data directory path */
  const char* data_dir = (argc > 1) ? argv[1] : "/tmp";

  DFTRACER_C_METADATA(meta, "mpi_test", "mpi_io_test");

  /* Test MPI_Barrier */
  MPI_Barrier(MPI_COMM_WORLD);

  /* Test MPI_Sendrecv */
  if (size >= 2) {
    int send_buf = rank;
    int recv_buf;
    int dest = (rank + 1) % size;
    int src = (rank - 1 + size) % size;

    MPI_Sendrecv(&send_buf, 1, MPI_INT, dest, 0, &recv_buf, 1, MPI_INT, src, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  }

  /* Test MPI_Bcast */
  int bcast_value = rank;
  MPI_Bcast(&bcast_value, 1, MPI_INT, 0, MPI_COMM_WORLD);

  /* Test MPI_Reduce */
  int reduce_result;
  MPI_Reduce(&rank, &reduce_result, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

  /* Test MPI_Allreduce */
  int allreduce_result;
  MPI_Allreduce(&rank, &allreduce_result, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  /* Test MPI-IO: Open, write, read, close */
  snprintf(filename, sizeof(filename), "%s/test_mpi_io_%d.bin", data_dir, rank);

  /* Create and write to file using MPI-IO */
  int mpi_err =
      MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_CREATE | MPI_MODE_WRONLY,
                    MPI_INFO_NULL, &fh);

  if (mpi_err == MPI_SUCCESS) {
    char write_buf[BUFFER_SIZE];
    memset(write_buf, rank + 'A', BUFFER_SIZE);

    MPI_File_write(fh, write_buf, BUFFER_SIZE, MPI_CHAR, &status);
    MPI_File_close(&fh);

    /* Read back the file */
    mpi_err = MPI_File_open(MPI_COMM_SELF, filename, MPI_MODE_RDONLY,
                            MPI_INFO_NULL, &fh);

    if (mpi_err == MPI_SUCCESS) {
      char read_buf[BUFFER_SIZE];
      MPI_File_read(fh, read_buf, BUFFER_SIZE, MPI_CHAR, &status);
      MPI_File_close(&fh);
    }
  }

  /* Test collective communication */
  int* sendbuf = NULL;
  int* recvbuf = NULL;

  if (rank == 0) {
    sendbuf = (int*)malloc(size * sizeof(int));
    recvbuf = (int*)malloc(size * sizeof(int));
    for (int i = 0; i < size; i++) {
      sendbuf[i] = i;
    }
  }

  int scatter_val;
  MPI_Scatter(sendbuf, 1, MPI_INT, &scatter_val, 1, MPI_INT, 0, MPI_COMM_WORLD);

  int gather_val = scatter_val * 2;
  MPI_Gather(&gather_val, 1, MPI_INT, recvbuf, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if (rank == 0) {
    free(sendbuf);
    free(recvbuf);
  }

  /* Final barrier */
  MPI_Barrier(MPI_COMM_WORLD);

  /* Finalize dftracer if initialized */
  if (init_dftracer) {
    DFTRACER_C_FINI();
  }

  /* Finalize MPI */
  MPI_Finalize();

  if (rank == 0) {
    printf("MPI test completed successfully\n");
  }

  return 0;
}
