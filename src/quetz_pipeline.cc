// Copyright 2009-2026 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2026, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "quetz_pipeline.h"

#include <inttypes.h>
#include <algorithm>

using namespace SST;
using namespace SST::Quetz;

QuetzEventPipeline::QuetzEventPipeline(QuetzCoreContext& ctx,
                                       PipelineInput*    input,
                                       PipelineFilter*   filter,
                                       PipelineTransform* transform,
                                       PipelineOutput*   output)
    : ctx_(ctx),
      input_(input),
      filter_(filter),
      transform_(transform),
      output_(output),
      emitter_(ctx.comp, ctx.out, ctx.core_id, ctx.tc,
               ctx.cache_line_size, ctx.check_addresses, *ctx.stats),
      inst_count_(0),
      halted_(false),
      refill_hit_cap_(false)
{
    if (ctx_.max_queue_len == 0 || ctx_.max_issue_per_cycle == 0 || ctx_.max_pending == 0 ||
        ctx_.cache_line_size == 0)
        ctx_.out->fatal(CALL_INFO, -1,
            "Pipeline queue, issue, pending and cache-line limits must be nonzero.\n");
    output_->configure(ctx_, emitter_);
    ctx.out->verbose(CALL_INFO, 1, 0,
        "QuetzEventPipeline %" PRIu32 " created: maxQ=%" PRIu32
        " maxIssue=%" PRIu32 " maxPend=%" PRIu32 "\n",
        ctx.core_id, ctx.max_queue_len, ctx.max_issue_per_cycle,
        ctx.max_pending);
}

bool QuetzEventPipeline::checkMaxInsts() {
    if (ctx_.max_insts > 0 && inst_count_ >= ctx_.max_insts) {
        ctx_.out->verbose(CALL_INFO, 1, 0,
            "QuetzCore %" PRIu32 " reached max_insts %" PRIu64 " — halting.\n",
            ctx_.core_id, ctx_.max_insts);
        halted_ = true;
        return true;
    }
    return false;
}

void QuetzEventPipeline::tick() {
    if (halted_)
        return;

    input_->refill(coreQ_, ctx_.max_queue_len, ctx_.out);

    // If refill filled the staging queue to its cap, the tunnel may still hold
    // more commands that didn't fit. The issue loop below can drain coreQ_ to
    // empty when max_issue_per_cycle is large, so coreQ_.empty() alone does not
    // prove the tunnel is exhausted — record the cap-stop so isDrained() does
    // not declare a false drain (which would let the accelerator port forward a
    // doorbell before all pre-doorbell packet writes have been issued).
    refill_hit_cap_ =
        (ctx_.max_queue_len > 0 && coreQ_.size() >= ctx_.max_queue_len);

    uint32_t issued = 0;
    while (!coreQ_.empty()) {
        if (has_pending_op_) {
            if (!issueMemOp(issued)) goto done;
            if (halted_) goto done;
            continue;
        }
        PipelineEvent& ev = coreQ_.front();

        MemRegionHandler::Action region_action = MemRegionHandler::Action::FORWARD;
        if ((ev.cmd.cmd == QUETZ_CMD_READ || ev.cmd.cmd == QUETZ_CMD_WRITE) &&
            filter_->handle(ev.cmd, *ctx_.stats, region_action))
        {
            ctx_.stats->insn_count->addData(1);
            coreQ_.pop();
            inst_count_++;
            if (checkMaxInsts()) return;
            continue;
        }

        switch (transform_->process(ev, *ctx_.stats, pending_op_, region_action)) {
        case PipelineTransform::Result::HALT_EXIT:
            ctx_.out->verbose(CALL_INFO, 1, 0,
                "QuetzCore %" PRIu32 " processing EXIT — halting.\n",
                ctx_.core_id);
            guest_exit_ = ev.cmd.cmd == QUETZ_CMD_EXIT;
            halted_ = true;
            coreQ_.pop();
            return;

        case PipelineTransform::Result::STALL:
            goto done;

        case PipelineTransform::Result::CONSUMED_NOOP:
            coreQ_.pop();
            inst_count_++;
            if (checkMaxInsts()) return;
            continue;

        case PipelineTransform::Result::EMIT_MEMOP:
            has_pending_op_ = true;
            break;
        }
    }

done:
    if (issued > 0)
        ctx_.stats->active_cycles->addData(1);
    ctx_.stats->cycles->addData(1);
}

bool QuetzEventPipeline::issueMemOp(uint32_t& issued) {
    const uint32_t pending = output_->pendingCount();
    const uint32_t budget = std::min(
        issued < ctx_.max_issue_per_cycle ? ctx_.max_issue_per_cycle - issued : 0,
        pending < ctx_.max_pending ? ctx_.max_pending - pending : 0);
    if (budget == 0) return false;
    bool complete = false;
    const uint32_t count = output_->issueAvailable(pending_op_, budget, complete);
    if (count > budget)
        ctx_.out->fatal(CALL_INFO, -1, "Pipeline output exceeded its issue budget.\n");
    if (!complete && count == 0 && issued == 0 && pending == 0)
        ctx_.out->fatal(CALL_INFO, -1,
            "Pipeline output cannot split an access to fit maxissuepercycle/maxtranscore.\n");
    issued += count;
    if (!complete) return false;
    has_pending_op_ = false;
    coreQ_.pop();
    ctx_.stats->insn_count->addData(1);
    inst_count_++;
    checkMaxInsts();
    return true;
}

void QuetzEventPipeline::finish() {
    ctx_.out->verbose(CALL_INFO, 1, 0,
        "QuetzEventPipeline %" PRIu32 " finishing, %" PRIu32
        " transactions still pending.\n",
        ctx_.core_id, output_->pendingCount());
    filter_->finish(ctx_.out, ctx_.core_id);
}
