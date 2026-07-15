// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
// Copyright 2020,2022 OpenHW Group
// Copyright 2020 Datum Technology Corporation
//
// Licensed under the Solderpad Hardware Licence, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://solderpad.org/licenses/
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


`ifndef __UVME_CV32E20_INTERRUPT_NOISE_SV__
`define __UVME_CV32E20_INTERRUPT_NOISE_SV__

/**
 * Virtual sequence responsible for running a free-running stream of
 * random interrupt events to the DUT. These events are asynchronous with the main test program.
 */
class uvme_cv32e20_interrupt_noise_c extends uvme_cv32e20_base_vseq_c;

   // Free-running interrupt noise generation (all three threads below) is only allowed up to
   // NOISE_CUTOFF_TIME_NS in simulated time; a busy noise stream can otherwise perpetually starve the
   // core of the chance to drain pending interrupts and return to executing the test program,
   // potentially causing a livelock on corev_rand_interrupt_nested.
   //
   // Past this point:
   //  - gen_assert/gen_deassert (fire-and-forget UVMA_INTERRUPT_SEQ_ITEM_ACTION_DEASSERT/ASSERT)
   //    simply stop. UVMA_INTERRUPT_SEQ_ITEM_ACTION_ASSERT (driven by the gen_deassert thread --
   //    the two are cosmetically mislabeled, see uvma_interrupt_drv.sv) drives a line high and
   //    releases its tracking semaphore immediately with no self-clear
   //    (uvma_interrupt_drv_c::assert_irq()); the line only clears via a later DEASSERT lucky
   //    enough to target the same bit, or the DUT's irq_ack handshake. Left running past the
   //    cutoff, one of these can leave a line stuck pending indefinitely, which -- combined with
   //    a WFI-only interrupt-vector handler that does no masking -- produces a self-sustaining
   //    wfi -> mret -> re-take same still-pending line -> wfi livelock with no forward progress
   //    (observed on corev_rand_interrupt_exception).
   //  - gen_assert_until_ack keeps running, but gated one action per real WFI retirement (see
   //    wfi_monitor()/wait_for_noise_slot() below) instead of being cut off entirely -- a total
   //    cutoff strands any test whose generated program relies on an interrupt to wake a WFI
   //    forever asleep (observed as a 100ms watchdog timeout on corev_rand_interrupt_debug).
   //    This action always self-clears via the real irq_ack handshake
   //    (uvma_interrupt_drv_c::assert_irq_until_ack()), so it can't leave a line stuck the way
   //    the other two can.
   localparam longint unsigned NOISE_CUTOFF_TIME_NS = 10_000_000;

   // WFI instruction encoding (opcode 1110011, funct12 0x105, rs1=rd=x0), used by
   // wfi_monitor() to detect a WFI retirement over RVFI. This is done to sense the
   // WFI retirement rather than just the irq_ack handshake.
   localparam bit [31:0] WFI_INSTR_ENCODING = 32'h10500073;

   // One key is put() by wfi_monitor() each time the core retires a WFI instruction, and
   // get() by a noise-generation loop past the cutoff before it's allowed to issue its next
   // action. A semaphore (rather than a plain SV event) is required here: an event trigger
   // that occurs while no thread is yet parked on it is lost, which would permanently
   // deadlock the core in WFI the first time the three generator threads' independent
   // rand_delay()s all happened to still be in flight when the WFI retired.
   semaphore wfi_sem;

   rand int unsigned short_delay_wgt;
   rand int unsigned med_delay_wgt;
   rand int unsigned long_delay_wgt;
   rand int unsigned initial_delay_assert_until_ack;
   rand int unsigned initial_delay_assert;
   rand int unsigned initial_delay_deassert;

   rand bit [31:0]   reserved_irq_mask;

   `uvm_object_utils_begin(uvme_cv32e20_interrupt_noise_c)
   `uvm_object_utils_end

   constraint default_delay_c {
     soft short_delay_wgt == 2;
     soft med_delay_wgt == 5;
     soft long_delay_wgt == 3;
   }

   constraint valid_delay_c {
     short_delay_wgt != 0 || med_delay_wgt != 0 || long_delay_wgt != 0;
   }

   constraint valid_initial_delay_assert_until_ack_c {
     initial_delay_assert_until_ack dist { 0 :/ 1,
                                           [10:500] :/ 4,
                                           [500:1000] :/ 3};
   }

   constraint valid_initial_delay_assert_c {
     initial_delay_assert dist { 0 :/ 2,
                                 [10:500] :/ 4,
                                 [500:1000] :/ 3};
   }

   constraint valid_initial_delay_deassert_c {
     initial_delay_deassert dist { 0 :/ 2,
                                   [10:500] :/ 4,
                                   [500:1000] :/ 3};
   }

   /**
    * Default constructor.
    */
   extern function new(string name="uvme_cv32e20_interrupt_noise");

   /**
    * Starts the clock, waits, then resets the DUT.
    */
   extern virtual task body();
   extern virtual task rand_delay();

   /**
    * Watches the RVFI instruction retirement interface and triggers wfi_retired_e each
    * time a WFI instruction retires.
    */
   extern virtual task wfi_monitor();

   /**
    * Called at the top of each gen_assert_until_ack loop iteration (the only noise thread
    * still running past the cutoff -- see NOISE_CUTOFF_TIME_NS above). Returns immediately
    * before NOISE_CUTOFF_TIME_NS; after it, blocks until the core retires a WFI.
    */
   extern virtual task wait_for_noise_slot();
endclass : uvme_cv32e20_interrupt_noise_c

function uvme_cv32e20_interrupt_noise_c::new(string name="uvme_cv32e20_interrupt_noise");

   super.new(name);

   wfi_sem = new(0);

endfunction : new

task uvme_cv32e20_interrupt_noise_c::rand_delay();
  randcase
    // SVTB.29.1.3.1 - Banned random number system functions and methods calls
    // Waive because the calls to the sys fns are controlled by constrained vars.
    //@DVT_LINTER_WAIVER_START "MT20211214_1" disable SVTB.29.1.3.1
    short_delay_wgt: repeat($urandom_range(   100,    1)) @(cntxt.interrupt_cntxt.vif.drv_cb);
    med_delay_wgt:   repeat($urandom_range(   500,  100)) @(cntxt.interrupt_cntxt.vif.drv_cb);
    long_delay_wgt:  repeat($urandom_range(10_000,5_000)) @(cntxt.interrupt_cntxt.vif.drv_cb);
    //@DVT_LINTER_WAIVER_END "MT20211214_1"
  endcase
endtask : rand_delay

task uvme_cv32e20_interrupt_noise_c::wfi_monitor();
  bit               last_order_valid = 0;
  longint unsigned  last_order;

  forever begin
    @(cntxt.rvfi_cntxt.instr_vif[0].mon_cb);
    // rvfi_valid/rvfi_insn hold their last-sampled value rather than pulsing back to an
    // invalid state, so once the core stops retiring (e.g. genuinely parked in WFI) this
    // condition would otherwise stay spuriously true on every subsequent clock -- checking
    // rvfi_order actually advancing is what distinguishes a real new retirement.
    if (cntxt.rvfi_cntxt.instr_vif[0].mon_cb.rvfi_valid &&
        (!last_order_valid || cntxt.rvfi_cntxt.instr_vif[0].mon_cb.rvfi_order != last_order)) begin
      last_order       = cntxt.rvfi_cntxt.instr_vif[0].mon_cb.rvfi_order;
      last_order_valid = 1;
      if (cntxt.rvfi_cntxt.instr_vif[0].mon_cb.rvfi_insn == WFI_INSTR_ENCODING) begin
        wfi_sem.put(1);
      end
    end
  end
endtask : wfi_monitor

task uvme_cv32e20_interrupt_noise_c::wait_for_noise_slot();
  if ($time >= NOISE_CUTOFF_TIME_NS) begin
    wfi_sem.get(1);
  end
endtask : wait_for_noise_slot

task uvme_cv32e20_interrupt_noise_c::body();

  fork
    begin : wfi_monitor_thread
      wfi_monitor();
    end

    begin : gen_assert_until_ack

      repeat (initial_delay_assert_until_ack) @(cntxt.interrupt_cntxt.vif.drv_cb);

      while (1) begin
        uvma_interrupt_seq_item_c irq_req;

        wait_for_noise_slot();

        `uvm_create_on(irq_req, p_sequencer.interrupt_sequencer);
        start_item(irq_req);
        irq_req.default_repeat_count_c.constraint_mode(0);
        assert(irq_req.randomize() with {
          action        == UVMA_INTERRUPT_SEQ_ITEM_ACTION_ASSERT_UNTIL_ACK;
          repeat_count dist { 1 :/ 9, [2:3] :/ 1 };

          (irq_mask & local::reserved_irq_mask) == 0;
        });
        finish_item(irq_req);

        rand_delay();

      end
    end

    begin : gen_assert

      repeat (initial_delay_assert) @(cntxt.interrupt_cntxt.vif.drv_cb);

      while ($time < NOISE_CUTOFF_TIME_NS) begin
        uvma_interrupt_seq_item_c irq_req;

        `uvm_do_on_with(irq_req, p_sequencer.interrupt_sequencer, {
          action        == UVMA_INTERRUPT_SEQ_ITEM_ACTION_DEASSERT;
          (irq_mask & local::reserved_irq_mask) == 0;
        })

        rand_delay();

      end
    end

    begin : gen_deassert

      repeat (initial_delay_deassert) @(cntxt.interrupt_cntxt.vif.drv_cb);

      while ($time < NOISE_CUTOFF_TIME_NS) begin
        uvma_interrupt_seq_item_c irq_req;

        `uvm_do_on_with(irq_req, p_sequencer.interrupt_sequencer, {
          action        == UVMA_INTERRUPT_SEQ_ITEM_ACTION_ASSERT;
          (irq_mask & local::reserved_irq_mask) == 0;
        })

        rand_delay();

      end
    end
  join
endtask : body

`endif // __UVME_CV32E20_INTERRUPT_NOISE_SV__
