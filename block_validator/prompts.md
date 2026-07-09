Prompt1
Please put the new code in the `experimental/mogo/test/ssd/` directory. I want
to write a c++ tool that will continuously validate a block storage device
contract. We will be performing a VM live migration in the background, that's
why we need a test that would run for a non-trivial amount of time because we
want to cover all: source side migration, blackout and target side migration.
The test should perform writes to the block device, I want it to prioritize
smaller writes because large writes consume a lot of bandwidth and limit the
IOPS, something like 50% of the writes should be <=16 KiB, 90% <=64 KiB and only
10% >64KiB and <128 KiB. I want the test to have a larger than normal chance of
issuing overlapping writes, probably up to 5% of writes should partially or
fully overlap in-flight writes. I want to have flags that control the region of
the disk targeted by the IO. Besides the writes, the test should also perform
reads of various sizes, the reads should validate the disk contract properly,
for example a read should reflect the data previously written to the same block,
if there were overlapping writes then the read should reflect that any of the 2
writes data can be in the block and that the atomicity boundaries are respected,
for example 2 large overlapping writes can be partially applied, but the
application of the writes should be on atomic write unit boundary. To validate
the data we can either store the hash of the data or we can store the seed used
to generate the data or maybe both so we can re-construct the data that was
supposed to be in the block.

Prompt2
Can you add a unit tests. Emulate the block device to be tested in a ramdisk.
Add failure detection tests as well

Prompt Trim
If the specified target is a raw block device, I would like the code to issue 5%
TRIM and 45% write commands. There are 2 caveats regarding trim requests:
1. Some devices guarantee that trimmed blocks return zeroes others don't, so we
   need a flag specifying whether block_validator should validate the contents
   of trimmed blocks against zeroes.
2. Trims are a hint for NVMe devices, so trimmed blocks may not actually change
   their content as a result of a trim operation. Ensure that the validation
   logic takes into account this fact.
TRIM operations should follow the overlapping logic of the writes.