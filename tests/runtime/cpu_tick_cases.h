// Compile QuetzCPU::tick intact with the real pipeline/input below these stubs.
// checkChild injects the allowed interleaving after refill observes an empty
// tunnel: the producer publishes EXIT and exits before waitpid reaps it.
using namespace SST;
using namespace SST::Quetz;

struct TickCore {
    QuetzEventPipeline* pipeline;
    bool isCoreHalted() const { return pipeline->isHalted(); }
    bool isDrained() const { return pipeline->isDrained(); }
    unsigned pendingCount() const { return pipeline->pendingCount(); }
    void tick() { pipeline->tick(); }
    void recordAsyncOverlapCycle() {}
};

struct TickFrontend {
    QuetzCoreBackend backend;
    bool publish_exit;
    unsigned checks = 0;
    QuetzCoreBackend* coreBackend() { return &backend; }
    bool checkChild() {
        if (checks++ == 0) {
            assert(backend.reads == 1 && backend.tunnel.empty());
            if (publish_exit) {
                QuetzCommand exit{};
                exit.cmd = QUETZ_CMD_EXIT;
                backend.tunnel.push(exit);
            }
        }
        return false;
    }
};

struct QuetzCPU {
    TickFrontend* frontend_;
    Output* output_;
    struct { unsigned vcpu_count = 1; } cfg_;
    std::vector<TickCore*> cores_;
    struct Accelerator { void process() {} };
    std::vector<Accelerator*> accel_ports_;
    uint64_t child_poll_ticks_ = 1023;
    bool child_running_ = true, stop_ticking_ = false;
    unsigned halted_count_ = 0;
    struct { bool active() const { return false; } } window_caches_;
    std::vector<int> generic_pending_;
    uint64_t getCurrentSimTimeNano() { return 0; }
    void pollMmioSyncMailbox() {}
    bool asyncOutstandingForVcpu(unsigned) { return false; }
    bool hasAsyncInFlight() { return false; }
    void primaryComponentOKToEndSim() {}
    bool tick(SST::Cycle_t);
};

struct TickFilter: PipelineFilter {
    explicit TickFilter(Params& p): PipelineFilter(0, p) {}
    void configure(const QuetzCoreContext&) override {}
    bool handle(const QuetzCommand&, QuetzCoreStats&, MemRegionHandler::Action&) override { return false; }
    void finish(Output*, uint32_t) override {}
};

void childExitRefillCase() {
    for (bool publish_exit: {false, true}) {
        Output log;
        Params params;
        Stats stats;
        ComponentExtension component;
        TickFrontend frontend;
        frontend.publish_exit = publish_exit;
        QuetzCoreContext context{};
        uint32_t latency[QUETZ_INSN_CLASS_COUNT]{};
        context.comp = &component;
        context.out = &log;
        context.stats = &stats;
        context.backend = &frontend.backend;
        context.cache_line_size = 64;
        context.max_issue_per_cycle = context.max_pending = 1;
        context.max_queue_len = 2;
        context.exec_latency = context.compute_latency = latency;
        DefaultPipelineInput input(0, params);
        input.configure(context);
        TickFilter filter(params);
        DefaultPipelineOutput output(0, params);
        DefaultPipelineTransform transform(0, params);
        transform.configure(context);
        QuetzEventPipeline pipeline(context, &input, &filter, &transform, &output);
        TickCore core{&pipeline};
        QuetzCPU cpu;
        cpu.frontend_ = &frontend;
        cpu.output_ = &log;
        cpu.cores_.push_back(&core);

        // No cached drain result may prove a missing EXIT in the same tick
        // that first observes process death, even if refill just saw empty.
        assert(!cpu.tick(0));
        assert(frontend.backend.reads == 1);
        assert(!pipeline.isHalted() && pipeline.isDrained());
        assert(frontend.backend.tunnel.size() == (publish_exit ? 1 : 0));
        bool rejected = false;
        try {
            assert(cpu.tick(1));
        } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("before all vCPU EXIT records") != std::string::npos;
            assert(rejected);
        }
        assert(frontend.backend.reads == 2);
        assert(rejected == !publish_exit);
        assert(pipeline.sawGuestExit() == publish_exit);
        assert(cpu.stop_ticking_ == publish_exit);
    }
}
