https://github.com/axboe/liburing/
https://kernel.dk/io_uring.pdf

defects of linux aio:
- 

> With a shared ring buffer, we could eliminate the need to have shared locking bewteen the application and the kernel, getting away with some clever use of memory ordering and barriers instead.

Add paddings to the tail to keep the size is times of 64 bytes.

Relying on natural wrapping of 32-bit integers.