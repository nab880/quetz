using namespace SST; using namespace SST::Quetz; using namespace SST::Interfaces;
struct Input: PipelineInput {
    std::queue<PipelineEvent> source;
    Input(): PipelineInput(0, params) {}
    inline static Params params;
    void configure(const QuetzCoreContext&) override {}
    bool refill(std::queue<PipelineEvent>& q,uint32_t max,Output*) override {
        while(q.size()<max && !source.empty()) { q.push(source.front()); source.pop(); }
        return false;
    }
    void push(QuetzShmemCmd cmd,uint64_t address=0,uint32_t size=0) {
        QuetzCommand c{}; c.cmd=cmd; c.addr=address; c.size=size;
        for (uint32_t i=0;i<sizeof c.data;++i) c.data[i]=static_cast<uint8_t>(i+1);
        source.push({c,0});
    }
};
struct Filter: PipelineFilter {
    unsigned calls=0;
    Filter(): PipelineFilter(0, Input::params) {}
    void configure(const QuetzCoreContext&) override {}
    bool handle(const QuetzCommand&,QuetzCoreStats&,MemRegionHandler::Action&) override { ++calls; return false; }
    void finish(Output*,uint32_t) override {}
};
void pipelineCase(unsigned issue,unsigned pending,unsigned qlen,bool write,uint32_t bytes,uint64_t maxinst=0) {
    Output log; Stats stats; ComponentExtension component; QuetzCoreContext c{};
    c.comp=&component; c.out=&log; c.stats=&stats; c.cache_line_size=64;
    c.max_issue_per_cycle=issue; c.max_pending=pending; c.max_queue_len=qlen; c.max_insts=maxinst;
    Input input; Filter filter; DefaultPipelineOutput output(0,Input::params);
    DefaultPipelineTransform transform(0,Input::params); transform.configure(c);
    input.push(write?QUETZ_CMD_WRITE:QUETZ_CMD_READ,60,bytes);
    input.push(QUETZ_CMD_EXIT);
    QuetzEventPipeline pipe(c,&input,&filter,&transform,&output);
    StandardMem mem; pipe.setMemLink(&mem);
    std::vector<uint8_t> written; unsigned requests=0; uint64_t address=60;
    for (unsigned tick=0;tick<10000 && !(pipe.isHalted()&&pipe.isDrained());++tick) {
        pipe.tick();
        assert(mem.sent.size()<=issue); assert(pipe.pendingCount()<=pending);
        while (!mem.sent.empty()) {
            auto* req=mem.sent.front();mem.sent.pop_front();
            assert(req->pAddr==address && req->size && req->pAddr/64==(req->pAddr+req->size-1)/64);
            address+=req->size; ++requests;
            if (write) { auto* wr=dynamic_cast<StandardMem::Write*>(req); assert(wr); written.insert(written.end(),wr->data.begin(),wr->data.end()); }
            StandardMem::Request* resp=write?static_cast<StandardMem::Request*>(new StandardMem::WriteResp(*req)):new StandardMem::ReadResp(*req);
            delete req;
            uint64_t latency;bool read,mmio; assert(pipe.handleResponse(resp,latency,read,mmio));
        }
    }
    assert(pipe.isHalted() && pipe.isDrained()); assert(filter.calls==1);
    const uint32_t actual=write?std::min<uint32_t>(bytes,sizeof(QuetzCommand::data)):bytes;
    assert(address==60+actual && requests==memorySlots(60,actual,64));
    assert((write?stats.split_writes:stats.split_reads)->value==requests-1);
    assert(stats.insn_count->value==1);
    if (write) for (size_t i=0;i<written.size();++i) assert(written[i]==static_cast<uint8_t>(i+1));
}
struct Host: AcceleratorHost {
    bool drained=false; unsigned acks=0,completions=0; StandardMem memory,mmio;
    void postResponse(uint32_t,uint64_t) override { ++acks; }
    void sendMem(uint32_t,StandardMem::Request* r) override { memory.send(r); }
    void sendMmio(uint32_t,StandardMem::Request* r) override { mmio.send(r); }
    bool isDrained(uint32_t) const override { return drained; }
    bool hasMem(uint32_t) const override { return true; }
    bool hasMmio(uint32_t) const override { return true; }
    uint64_t cacheLineSize() const override { return 64; }
    uint64_t cycles() const override { return 10; }
    void recordSyncRequest(uint32_t,bool) override {}
    void recordDoorbellFlush(uint32_t) override {}
    void recordDoorbellFlushCycles(uint32_t,uint64_t) override {}
    void recordAsyncSubmit(uint32_t) override {}
    void recordAsyncCompletion(uint32_t) override { ++completions; }
    void flush(BalarAcceleratorPort& port) {
        while (!memory.sent.empty()) { auto* req=memory.sent.front();memory.sent.pop_front();auto* resp=new StandardMem::FlushResp(*req);delete req;assert(port.handleResponse(0,resp)); }
    }
    void retire(BalarAcceleratorPort& port) {
        assert(!mmio.sent.empty());auto* req=mmio.sent.front();mmio.sent.pop_front();auto* resp=new StandardMem::WriteResp(*req);delete req;assert(port.handleResponse(0,resp));
    }
};
void asyncCase() {
    Params params; params.values={{"doorbell_addr","4096"},{"async_offload","1"},{"async_doorbell_addr","8192"},{"async_completion_depth","3"},{"packet_flush_bytes","128"}};
    Host host; BalarAcceleratorPort port(0,params,&host);
    QuetzCommand cmd{};cmd.cmd=QUETZ_CMD_MMIO_WRITE_REQ;cmd.addr=8192;cmd.size=8;
    for(unsigned n=0;n<3;++n) {
        host.drained=false;port.handleCommand(0,cmd);port.process();
        assert(host.acks==n && host.memory.sent.empty());
        host.drained=true;port.process();assert(host.memory.sent.size()==2);assert(host.acks==n);
        host.flush(port);assert(host.acks==n+1 && host.mmio.sent.size()==1);
    }
    // Continuous post-submit CPU traffic (or a halted CPU with no future
    // ticks) cannot strand queued work: dispatch no longer requests a drain.
    host.drained=false;
    for(unsigned n=0;n<3;++n) host.retire(port);
    assert(host.completions==3 && host.acks==3 && !port.hasOutstanding());
}
void gpuCase() {
    for(uint64_t line: {0ULL,16ULL,32ULL,64ULL,128ULL}) for(uint64_t offset: {0ULL,4ULL,8ULL,15ULL}) {
        StandardMem mem;mem.line_size=line;QuetzGpuDevice gpu;gpu.mem_iface_=&mem;
        gpu.op_args_={0x90000000+offset,0x90001000+offset};gpu.op_in_bytes_=128;
        assert(gpu.dmaRangeOk(gpu.op_args_.src_addr,128));gpu.opIssueReadWindow();
        auto check=[&](uint64_t address, bool write) {
            size_t size=0;
            while(!mem.sent.empty()) {
                auto* req=mem.sent.front();mem.sent.pop_front();
                assert(req->pAddr==address+size && req->size>0 && req->size<=64);
                if(line) assert(req->pAddr/line==(req->pAddr+req->size-1)/line);
                if(write) { auto* wr=dynamic_cast<StandardMem::Write*>(req);assert(wr);for(size_t i=0;i<wr->data.size();++i)assert(wr->data[i]==uint8_t(size+i)); }
                size+=req->size;delete req;
            }
            assert(size==128);
        };
        check(gpu.op_args_.src_addr,false);
        gpu.op_out_.resize(128);for(size_t i=0;i<128;++i)gpu.op_out_[i]=uint8_t(i);
        gpu.op_next_dma_off_=gpu.op_dma_outstanding_=0;gpu.opIssueWriteWindow();check(gpu.op_args_.dst_addr,true);
    }
    QuetzGpuDevice gpu;gpu.dma_range_start_=gpu.dma_range_end_=0;
    assert(!gpu.dmaRangeOk(UINT64_MAX-31,128));assert(gpu.dmaRangeOk(UINT64_MAX-31,32));
}
void childCase() {
    Output log;
    for(int exit_code: {0,7}) {
        QemuLauncher child(&log);child.pid_=fork();assert(child.pid_>=0);
        if(child.pid_==0) _exit(exit_code);
        bool failed=false;
        try { for(int n=0;n<1000 && child.checkChild();++n) usleep(1000); }
        catch(const std::runtime_error& e) { failed=std::string(e.what()).find("status 7")!=std::string::npos; }
        assert(child.pid_==0 && failed==(exit_code!=0));
    }
    // The EXIT callback precedes process exit. A delayed failure must survive
    // normal simulator teardown instead of being replaced by our SIGTERM.
    for(int exit_code: {0,7}) {
        QemuLauncher child(&log);child.pid_=fork();assert(child.pid_>=0);
        if(child.pid_==0) { usleep(50000); _exit(exit_code); }
        bool failed=false;
        try { child.terminate(true); } catch(const std::runtime_error&) { failed=true; }
        assert(child.pid_==0 && failed==(exit_code!=0));
    }
    QemuLauncher child(&log);child.pid_=fork();assert(child.pid_>=0);
    if(child.pid_==0) {raise(SIGKILL);_exit(0);}
    bool failed=false;try { while(child.checkChild()) usleep(1000); } catch(const std::runtime_error& e) { failed=std::string(e.what()).find("signal")!=std::string::npos; }
    assert(failed && child.pid_==0);
}
int main() {
    pipelineCase(1,16,64,false,8);pipelineCase(2,1,64,false,8);
    pipelineCase(1,1,2,false,8);pipelineCase(1,1,64,false,1024);
    pipelineCase(1,1,2,true,16);pipelineCase(1,1,64,true,1024);
    pipelineCase(1,1,64,false,8,1);
    asyncCase();gpuCase();childCase();childExitRefillCase();
    puts("PASS production pipeline, emitter, async queue, GPU DMA and child-exit regressions");
}
