# MVLC command syntax

| Command                        | Args                                       | Notes                                                      |
| ------------------------------ | ------------------------------------------ | ---------------------------------------------------------- |
| **vme_read**                   | `amod` `data_width` `address` ['late']     |                                                            |
| **vme_read_mem**               | `amod` `data_width` `address` ['late']     |                                                            |
| **vme_block_read**             | `amod` `transfers` `address` [`esst_rate`] |                                                            |
| **vme_block_read_swapped**     | `amod` `transfers` `address` [`esst_rate`] |                                                            |
| **vme_block_read_mem**         | `amod` `transfers` `address`               |                                                            |
| **vme_block_read_mem_swapped** | `amod` `transfers` `address`               |                                                            |
| **vme_write**                  | `amod` `data_width` `address` `value`      |                                                            |
| **write_marker**               | `value`                                    | 32-bit marker value to insert into the output stream       |
| **write_special**              | `value`                                    | 0=timestamp, 1=accu value                                  |
| **wait**                       | `cycles`                                   | 24 valid bits in units of MVLC clock cycles                |
| **signal_accu**                |                                            | Create MVLC internal signal  the current accu value.       |
| **mask_shift_accu**            | `mask` `shift`                             | Modify accu value. Mask first, then **rotate** left.       |
| **set_accu**                   | `value`                                    | Set the accu to a fixed value.                             |
| **read_to_accu**               | `amod` `data_width` `address` ['late']     | VME read storing the result in the accu.                   |
| **compare_loop_accu**          | `cmp` `value`                              | Compare accu vs given value. Loop to prev read on failure. |
| **software_delay**             | `delay_ms`                                 |                                                            |

* `amod`: numerical VME address modifier. Must match the transfer type.
  Passing a non-block amod to a block read command is an error.
* `data_width`: `d16` or `d32`.
* `esst_rate`: numeric rate value. 0=160mb, 1=276mb, 2=320mb.
* The `swapped` variants swap the two 32-bit words in each 64-bit word.
* The `mem` variants do increment the read address. Non-`mem` means FIFO mode.
* `transfers` is an unsigned 16-bit value, so the max transfer count for block
  reads is 0xffff.
* `late` means to read out on the trailing edge of the DTACK signal instead of
  at the leading edge. Required for some modules, e.g. TRIVA.

## The stack accumulator

The **vme_read** and **vme_read_mem** commands usually return a single data word
not contained in a block read structure. When used in combination with the MVLC
stack accumulator these commands turn into block reads and produce block read
output structures.

```
# Reads 100 times from address 0x0000.
set_accu 100
vme_read 0x09 d16 0x0000

# Reads 100 times from the device, incrementing the address after each read.
set_accu 100
vme_read_mem 0x09 d16 0x0000
```

In combination with **read_to_accu** and **mask_shift_accu** this can be used to
read a transfer count from a module to create a fake block transfer.

**signal_accu** creates an MVLC internal IRQ signal using the current accu
value. This can be used to dispatch from one stack to another: read a value from
a module into the accu, optionally **mask_shift_accu** it to exctract a
trigger/IRQ number, then call **signal_accu** to dispatch to the actual handler
stack.

### mask_shift_accu

The **mask_shift_accu** operation is applied at the time the accumulator value
needs to be evaluated, for example when a **vme_read** instruction is processed.
This means **mask_shift_accu** can be set before the actual read to accu. Also
multiple **mask_shift_accu** commands do not modify the accu but instead the
last command applies it's mask and rotation to the accu at evaluation time.

### compare_loop_accu

This command needs to follow a **read_to_accu** or **set_accu** command.
Compares the current accu value against the static compare value. If the
comparison fails loops back to the previous read instruction. Otherwise
continues executing the next command.

Comparators: 0: equals, 1: less than, 2: greater than.

If the comparison fails loop will terminate after ~5µs and the **timeout** flag
will be set in the resulting **0xF3 StackFrame**

# VME block read command mappings

FIFO reads do not increment the read address, mem reads do. **VMEReadMem** and
**VMEReadMemSwapped** are available since MVLC firmware ``FW0036``.

| mvme VMEScript | MVLC YAML                  | MVLC command           | Notes                                       |
| -------------- | -------------------------- | ---------------------- | ------------------------------------------- |
| blt            | vme_block_read_mem         | 0x32 VMEReadMem        |                                             |
| bltfifo        | vme_block_read             | 0x12 VMERead           |                                             |
| mblt           | vme_block_read_mem         | 0x32 VMEReadMem        |                                             |
| mbltfifo       | vme_block_read             | 0x12 VMERead           |                                             |
| mblts          | vme_block_read_mem_swapped | 0x33 VMEReadMemSwapped |                                             |
| mbltsfifo      | vme_block_read_swapped     | 0x13 VMEReadSwapped    |                                             |
| 2esst          | vme_block_read             | 0x12 VMERead           | for compatibility this is *fifo*, not *mem* |
| 2esstfifo      | vme_block_read             | 0x12 VMERead           | same as `2esst`                             |
| 2esstmem       | vme_block_read_mem         | 0x32 VMEReadMem        | explicit *mem* version                      |
| 2essts         | vme_block_read_swapped     | 0x13 VMEReadSwapped    | for compatibility this is *fifo*, not *mem* |
| 2esstsfifo     | vme_block_read_swapped     | 0x13 VMEReadSwapped    | same as `2essts`                            |
| 2esstsmem      | vme_block_read_mem_swapped | 0x33 VMEReadMemSwapped | explicit *mem* version                      |
| read           | vme_read                   | 0x12 VMERead           | MVLC stack accu + `fifo` flag               |
| read           | vme_read_mem               | 0x32 VMEReadMem        | MVLC stack accu + `mem` flag                |

The mapping of the **read** command depends on whether the `fifo` or `mem` flags
are specified. This is only used in combination with the MVLC stack accumulator.
