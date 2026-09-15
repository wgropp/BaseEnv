!
! Beginnings of a shim file for an mpif08 implementation of the cartesian topology routines
! This must be written in Fortran 08 but call the C versions of MPIX_Nodecart_xxxx

subroutine MPI_Cart_create(comm_old, ndims, dims, periods, reorder, comm_cart, ierror)
use mpif08
TYPE(MPI_Comm), INTENT(IN) :: comm_old
INTEGER, INTENT(IN) :: ndims, dims(ndims)
LOGICAL, INTENT(IN) :: periods(ndims), reorder
TYPE(MPI_Comm), INTENT(OUT) :: comm_cart
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cartdim_get(comm, ndims, ierror)
use mpif08
TYPE(MPI_Comm), INTENT(IN) :: comm
INTEGER, INTENT(OUT) :: ndims
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cart_get(comm, maxdims, dims, periods, coords, ierror)
use mpif08
TYPE(MPI_Comm), INTENT(IN) :: comm
INTEGER, INTENT(IN) :: maxdims
INTEGER, INTENT(OUT) :: dims(maxdims), coords(maxdims)
LOGICAL, INTENT(OUT) :: periods(maxdims)
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cart_rank(comm, coords, rank, ierror)
TYPE(MPI_Comm), INTENT(IN) :: comm
INTEGER, INTENT(IN) :: coords(*)
INTEGER, INTENT(OUT) :: rank
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cart_coords(comm, rank, maxdims, coords, ierror)
TYPE(MPI_Comm), INTENT(IN) :: comm
INTEGER, INTENT(IN) :: rank, maxdims
INTEGER, INTENT(OUT) :: coords(maxdims)
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cart_shift(comm, direction, disp, rank_source, rank_dest, ierror)
TYPE(MPI_Comm), INTENT(IN) :: comm
INTEGER, INTENT(IN) :: direction, disp
INTEGER, INTENT(OUT) :: rank_source, rank_dest
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cart_sub(comm, remain_dims, newcomm, ierror)
TYPE(MPI_Comm), INTENT(IN) :: comm
LOGICAL, INTENT(IN) :: remain_dims(*)
TYPE(MPI_Comm), INTENT(OUT) :: newcomm
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end

subroutine MPI_Cart_map(comm, ndims, dims, periods, newrank, ierror)
TYPE(MPI_Comm), INTENT(IN) :: comm
INTEGER, INTENT(IN) :: ndims, dims(ndims)
LOGICAL, INTENT(IN) :: periods(ndims)
INTEGER, INTENT(OUT) :: newrank
INTEGER, OPTIONAL, INTENT(OUT) :: ierror
end


