
#include "fpga.h"

class AWSP2_Response : public AWSP2_ResponseWrapper {
private:
    AWSP2 *fpga;
public:
    AWSP2_Response(int id, AWSP2 *fpga) : AWSP2_ResponseWrapper(id), fpga(fpga) {
    }
    virtual void dmi_read_data(uint32_t rsp_data) {
        //fprintf(stderr, "dmi_read_data data=%08x\n", rsp_data);
        fpga->rsp_data = rsp_data;
        sem_post(&fpga->sem);
    }
    void ddr_data ( const bsvvector_Luint8_t_L64 data ) {
        memcpy(&fpga->pcis_rsp_data, data, 64);
        sem_post(&fpga->sem);
    }
    virtual void dmi_status_data(uint16_t rsp_data) {
        fpga->rsp_data = rsp_data;
        sem_post(&fpga->sem);
    }

    virtual void tandem_packet(const uint32_t num_bytes, const bsvvector_Luint8_t_L72 bytes);
    void io_awaddr(uint32_t awaddr, uint16_t awlen, uint16_t awid);
    void io_araddr(uint32_t araddr, uint16_t arlen, uint16_t arid);
    void console_putchar(uint64_t wdata);
    void io_wdata(uint64_t wdata, uint8_t wstrb);
};

void AWSP2_Response::tandem_packet(const uint32_t num_bytes, const bsvvector_Luint8_t_L72 bytes) 
{
    if (fpga->stop_capture)
        return;

    uint8_t packet[72];
    int matched_pc = 0;
    for (int i = 0; i < 72; i++)
        packet[i] = bytes[71 - i];
    //fprintf(stderr, "[TV] %x %x %x %x\n", packet[0], packet[1], packet[2], packet[11]);
    if (packet[0] == te_op_begin_group && packet[1] == te_op_state_init && packet[2] == te_op_mem_req && packet[11] == 0x21) {
        uint64_t addr = 0;
        uint32_t val = 0;
        memcpy(&addr, &packet[3], sizeof(addr));
        memcpy(&val, &packet[12], sizeof(val));
        if (fpga->display_tandem) fprintf(stderr, "[TV] write mem [%08lx] <- %08x\n", addr, val);
    } else if (packet[0] == te_op_begin_group) {
        int offset = 1;
        if (fpga->display_tandem) fprintf(stderr, "[TV]");
        while (offset < num_bytes) {
            if (packet[offset] == te_op_addl_state) {
                offset++;
                if (packet[offset] == te_op_addl_state_priv) {
                    offset++;
                    if (fpga->display_tandem) fprintf(stderr, " priv %x", packet[offset]);
                    offset++;
                } else if (packet[offset] == te_op_addl_state_pc) {
                    offset++;
                    uint64_t addr = 0;
                    memcpy(&addr, &packet[offset], sizeof(addr));
                    offset += sizeof(addr);
                    if ((addr & 0xffffffffffffffc0ul) == 0xffffffe0006591c0ul)
                        matched_pc = 1;
                    if (fpga->display_tandem) fprintf(stderr, " next pc %08lx", addr);
                    if (addr == 0x82000130) {
                        if (fpga->display_tandem) fprintf(stderr, " matched STOP pc %08lx, stopping capture", addr);
                        fpga->stop_capture = 1;
                    }
                } else if (packet[offset] == te_op_addl_state_data8) {
                    offset++;
                    uint8_t data = 0;
                    memcpy(&data, &packet[offset], sizeof(data));
                    offset += sizeof(data);
                    if (fpga->display_tandem) fprintf(stderr, " data8 %02x", data);
                } else if (packet[offset] == te_op_addl_state_data16) {
                    offset++;
                    uint16_t data = 0;
                    memcpy(&data, &packet[offset], sizeof(data));
                    offset += sizeof(data);
                    if (fpga->display_tandem) fprintf(stderr, " data16 %04x", data);
                } else if (packet[offset] == te_op_addl_state_data32) {
                    offset++;
                    uint32_t data = 0;
                    memcpy(&data, &packet[offset], sizeof(data));
                    offset += sizeof(data);
                    if (fpga->display_tandem) fprintf(stderr, " data32 %08x", data);
                } else if (packet[offset] == te_op_addl_state_data64) {
                    offset++;
                    uint64_t data = 0;
                    memcpy(&data, &packet[offset], sizeof(data));
                    offset += sizeof(data);
                    if (fpga->display_tandem) fprintf(stderr, " data64 %08lx", data);
                } else if (packet[offset] == te_op_addl_state_eaddr) {
                    offset++;
                    uint64_t data = 0;
                    memcpy(&data, &packet[offset], sizeof(data));
                    offset += sizeof(data);
                    if (fpga->display_tandem) fprintf(stderr, " eaddr %08lx", data);
                } else {
                    break;
                }
            } else if (packet[offset] == te_op_16b_instr) {
                offset++;
                uint16_t instr = 0;
                memcpy(&instr, &packet[offset], sizeof(instr));
                offset += sizeof(instr);
                if (fpga->display_tandem) fprintf(stderr, " instr %04x", instr);
            } else if (packet[offset] == te_op_32b_instr) {
                offset++;
                uint32_t instr = 0;
                memcpy(&instr, &packet[offset], sizeof(instr));
                offset += sizeof(instr);
                if (fpga->display_tandem) fprintf(stderr, " instr %08x", instr);
            } else if (packet[offset] == te_op_full_reg) {
                offset++;
                uint16_t regnum = 0;
                uint64_t regval = 0;
                memcpy(&regnum, &packet[offset], sizeof(regnum));
                offset += sizeof(regnum);
                memcpy(&regval, &packet[offset], sizeof(regval));
                offset += sizeof(regval);

                if (fpga->display_tandem) fprintf(stderr, " reg %x val %08lx", regnum, regval);
            } else if (packet[offset] == te_op_end_group) {
                offset++;
            } else {
                break;
            }
        }
        if (fpga->display_tandem) fprintf(stderr, "\n");

        if (offset < num_bytes) {
            if (fpga->display_tandem) fprintf(stderr, " len %d offset %d op %x\n", num_bytes, offset, packet[offset]);
            for (uint32_t i = 0; i < num_bytes; i++) {
                if (fpga->display_tandem) fprintf(stderr, " %02x", packet[i] & 0xFF);
            }
            if (fpga->display_tandem) fprintf(stderr, "\n");
        }
    } else {
            
        if (fpga->display_tandem) fprintf(stderr, "[TV] %d packet", num_bytes);
        if (num_bytes < 72) {
            for (uint32_t i = 0; i < num_bytes; i++) {
                if (fpga->display_tandem) fprintf(stderr, " %02x", packet[i] & 0xFF);
            }
        }
        if (fpga->display_tandem) fprintf(stderr, "\n");
    }
    if (matched_pc)
        fpga->display_tandem = 1;
}

void AWSP2_Response::io_awaddr(uint32_t awaddr, uint16_t awlen, uint16_t awid) {
    fpga->awaddr = awaddr;
    PhysMemoryRange *pr = fpga->virtio_devices.get_phys_mem_range(awaddr);
    if (pr) {
        uint32_t offset = awaddr - pr->addr;
        fprintf(stderr, "virtio awaddr %08x device addr %08lx offset %08x len %d\n", awaddr, pr->addr, offset, awlen);
    } else if (awaddr == 0x60000000) {
        // UART 
    } else if (awaddr == 0x10001000 || awaddr == 0x50001000) {
        // tohost
    } else if (awaddr == 0x10001008 || awaddr == 0x50001008) {
        // fromhost
    } else {
        fprintf(stderr, "io_awaddr awaddr=%08x awlen=%d\n", awaddr, awlen);
    }
    fpga->wdata_count = awlen / 8;
    fpga->wid = awid;
}

void AWSP2_Response::io_araddr(uint32_t araddr, uint16_t arlen, uint16_t arid)
{
    PhysMemoryRange *pr = fpga->virtio_devices.get_phys_mem_range(araddr);
    if (pr) {
        uint32_t offset = araddr - pr->addr;
        int size_log2 = 4;
        fprintf(stderr, "virtio araddr %08x device addr %08lx offset %08x len %d\n", araddr, pr->addr, offset, arlen);
        for (int i = 0; i < arlen / 8; i++) {
            int last = i == ((arlen / 8) - 1);
            //fprintf(stderr, "io_rdata %08lx\n", rom.data[offset + i]);
            uint32_t val = pr->read_func(pr->opaque, offset, size_log2);
            fpga->request->io_rdata(val, arid, 0, last);
            offset += 4;
        }
    } else if (fpga->rom.base <= araddr && araddr < fpga->rom.limit) {
        int offset = (araddr - fpga->rom.base) / 8;
        //fprintf(stderr, "rom offset %x data %08lx\n", (int)(araddr - rom.base), rom.data[offset]);
        for (int i = 0; i < arlen / 8; i++) {
            int last = i == ((arlen / 8) - 1);
            //fprintf(stderr, "io_rdata %08lx\n", rom.data[offset + i]);
            fpga->request->io_rdata(fpga->rom.data[offset + i], arid, 0, last);
        }
    } else {
        if (araddr != 0x10001000 && araddr != 0x10001008 && araddr != 0x50001000 && araddr != 0x50001008)
            fprintf(stderr, "io_araddr araddr=%08x arlen=%d\n", araddr, arlen);
        for (int i = 0; i < arlen / 8; i++) {
            int last = i == ((arlen / 8) - 1);
            // 0xbad0beef
            fpga->request->io_rdata(0, arid, 0, last);
        }       
    }
        
}

void AWSP2_Response::io_wdata(uint64_t wdata, uint8_t wstrb) {
    uint32_t awaddr = fpga->awaddr;
    PhysMemoryRange *pr = fpga->virtio_devices.get_phys_mem_range(awaddr);
    if (pr) {
        int size_log2 = 4;
        uint32_t offset = awaddr - pr->addr;
        pr->write_func(pr->opaque, offset, wdata, size_log2);
    } else if (awaddr == 0x60000000) {
        console_putchar(wdata);
    } else if (awaddr == 0x10001000 || awaddr == 0x50001000) {
        // tohost
        uint8_t dev = (wdata >> 56) & 0xFF;
        uint8_t cmd = (wdata >> 48) & 0xFF;
        uint64_t payload = wdata & 0x0000FFFFFFFFFFFFul;
        if (dev == 1 && cmd == 1) {
            // putchar
            console_putchar(payload);
        } else {
            fprintf(stderr, "\nHTIF: dev=%d cmd=%02x payload=%08lx\n", dev, cmd, payload);
        }
    } else if (awaddr == 0x10001008) {
        fprintf(stderr, "\nHTIF: awaddr %08x wdata=%08lx\n", awaddr, wdata);
    } else {
        fprintf(stderr, "io_wdata wdata=%lx wstrb=%x\n", wdata, wstrb);
    }
    fpga->wdata_count -= 1;
    if (fpga->wdata_count <= 0) {
        if (awaddr != 0x60000000
            && awaddr != 0x10001008
            && awaddr != 0x10001000
            && awaddr != 0x50001008)
            fprintf(stderr, "-> io_bdone %08x\n", awaddr);
        fpga->request->io_bdone(fpga->wid, 0);
    }
}

void AWSP2_Response::console_putchar(uint64_t wdata) {
    if (fpga->start_of_line) {
        printf("\nCONSOLE: ");
        fpga->start_of_line = 0;
    }
    fputc(wdata, stdout);
    //fprintf(stderr, "CONSOLE: %c\n", (int)wdata);
    if (wdata == '\n') {
        printf("\n");
        fflush(stdout);
        fpga->start_of_line = 1;
    }
}


AWSP2::AWSP2(int id, const Rom &rom) 
  : response(0), rom(rom), last_addr(0), wdata_count(0), wid(0), start_of_line(1)
{
    sem_init(&sem, 0, 0);
    response = new AWSP2_Response(id, this);
    request = new AWSP2_RequestProxy(IfcNames_AWSP2_RequestS2H);
    stop_capture = 0;
    display_tandem = 1;
}

AWSP2::~AWSP2() {
}

void AWSP2::capture_tv_info(int c, int display) {
    request->capture_tv_info(c);
    display_tandem = display;
}

void AWSP2::wait() {
    //fprintf(stderr, "fpga::wait\n");
    sem_wait(&sem);
}

uint32_t AWSP2::dmi_status() {
    request->dmi_status();
    wait();
    return rsp_data;
}

uint32_t AWSP2::dmi_read(uint32_t addr) {
    //fprintf(stderr, "sw dmi_read %x\n", addr);
    request->dmi_read(addr);
    wait();
    return rsp_data;
}

void AWSP2::dmi_write(uint32_t addr, uint32_t data) {
    request->dmi_write(addr, data);
    wait();
}

void AWSP2::ddr_read(uint32_t addr, bsvvector_Luint8_t_L64 data) {
    request->ddr_read(addr);
    wait();
    if (data)
        memcpy(data, pcis_rsp_data, 64);
}

void AWSP2::ddr_write(uint32_t addr, const bsvvector_Luint8_t_L64 data, uint64_t wstrb) {
    request->ddr_write(addr, data, wstrb);
    wait();
}

void AWSP2::register_region(uint32_t region, uint32_t objid) {
    request->register_region(region, objid);
}

void AWSP2::memory_ready() {
    request->memory_ready();
}

uint64_t AWSP2::read_csr(int i) {
    dmi_write(DM_COMMAND_REG, DM_COMMAND_ACCESS_REGISTER | (3 << 20) | (1 << 17) | i);
    uint64_t val = dmi_read(5);
    val <<=  32;
    val |= dmi_read(4);
    return val;
}

void AWSP2::write_csr(int i, uint64_t val) {
    dmi_write(5, (val >> 32) & 0xFFFFFFFF);
    dmi_write(4, (val >>  0) & 0xFFFFFFFF);
    dmi_write(DM_COMMAND_REG, DM_COMMAND_ACCESS_REGISTER | (3 << 20) | (1 << 17) | (1 << 16) | i);
}

uint64_t AWSP2::read_gpr(int i) {
    dmi_write(DM_COMMAND_REG, DM_COMMAND_ACCESS_REGISTER | (3 << 20) | (1 << 17) | 0x1000 | i);
    uint64_t val = dmi_read(5);
    val <<=  32;
    val |= dmi_read(4);
    return val;
}

void AWSP2::write_gpr(int i, uint64_t val) {
    dmi_write(5, (val >> 32) & 0xFFFFFFFF);
    dmi_write(4, (val >>  0) & 0xFFFFFFFF);
    dmi_write(DM_COMMAND_REG, DM_COMMAND_ACCESS_REGISTER | (3 << 20) | (1 << 17) | (1 << 16) | 0x1000 | i);
}

void AWSP2::sbcs_wait() {
    uint32_t sbcs = 0;
    int count = 0;
    do {
        sbcs = dmi_read(DM_SBCS_REG);
        count++;
        if (sbcs & (SBCS_SBBUSYERROR)) {
            fprintf(stderr, "ERROR: sbcs=%x\n", sbcs);
        }
        if (sbcs & (SBCS_SBBUSY)) {
            fprintf(stderr, "BUSY sbcs=%x %d\n", sbcs, count);
        }
    } while (sbcs & (SBCS_SBBUSY));
}
uint32_t AWSP2::read32(uint32_t addr) {
    if (1 || last_addr != addr) {
        dmi_write(DM_SBCS_REG, SBCS_SBACCESS32 | SBCS_SBREADONADDR | SBCS_SBREADONDATA | SBCS_SBAUTOINCREMENT | SBCS_SBBUSYERROR);
        sbcs_wait();
        dmi_write(DM_SBADDRESS0_REG, addr);
    }
    sbcs_wait();
    dmi_write(DM_SBCS_REG, 0);
    sbcs_wait();
    uint64_t lo = dmi_read(DM_SBDATA0_REG);
    last_addr = addr + 4;
    return lo;
}

uint64_t AWSP2::read64(uint32_t addr) {
    if (1 || last_addr != addr) {
        dmi_write(DM_SBCS_REG, SBCS_SBACCESS32 | SBCS_SBREADONADDR | SBCS_SBREADONDATA | SBCS_SBAUTOINCREMENT | SBCS_SBBUSYERROR);
        sbcs_wait();
        dmi_write(DM_SBADDRESS0_REG, addr);
    }
    sbcs_wait();
    uint64_t lo = dmi_read(DM_SBDATA0_REG);
    sbcs_wait();
    dmi_write(DM_SBCS_REG, 0);
    sbcs_wait();
    uint64_t hi = dmi_read(DM_SBDATA0_REG);
    last_addr = addr + 8;
    return (hi << 32) | lo;
}

void AWSP2::write32(uint32_t addr, uint32_t val) {
    if (1 || last_addr != addr) {
        dmi_write(DM_SBCS_REG, SBCS_SBACCESS32);
        dmi_write(DM_SBADDRESS0_REG, addr);
    }
    sbcs_wait();
    dmi_write(DM_SBDATA0_REG, (val >>  0) & 0xFFFFFFFF);
    last_addr = addr + 4;
}

void AWSP2::write64(uint32_t addr, uint64_t val) {
    if (1 || last_addr != addr) {
        dmi_write(DM_SBCS_REG, SBCS_SBACCESS32 | SBCS_SBAUTOINCREMENT);
        dmi_write(DM_SBADDRESS0_REG, addr);
    }
    sbcs_wait();
    dmi_write(DM_SBDATA0_REG, (val >>  0) & 0xFFFFFFFF);
    sbcs_wait();
    dmi_write(DM_SBDATA0_REG, (val >>  32) & 0xFFFFFFFF);
    last_addr = addr + 8;
    sbcs_wait();
    dmi_write(DM_SBCS_REG, 0);
}

void AWSP2::halt(int timeout) {
    dmi_write(DM_CONTROL_REG, DM_CONTROL_HALTREQ | dmi_read(DM_CONTROL_REG));
    for (int i = 0; i < 100; i++) {
        uint32_t status = dmi_read(DM_STATUS_REG);
        if (status & (1 << 9))
            break;
    }
    dmi_write(DM_CONTROL_REG, ~DM_CONTROL_HALTREQ & dmi_read(DM_CONTROL_REG));
}
void AWSP2::resume(int timeout) {
    dmi_write(DM_CONTROL_REG, DM_CONTROL_RESUMEREQ | dmi_read(DM_CONTROL_REG));
    for (int i = 0; i < 100; i++) {
        uint32_t status = dmi_read(DM_STATUS_REG);
        if (status & (1 << 17))
            break;
    }
    dmi_write(DM_CONTROL_REG, ~DM_CONTROL_RESUMEREQ & dmi_read(DM_CONTROL_REG));
}

void AWSP2::set_fabric_verbosity(uint8_t verbosity) {
    request->set_fabric_verbosity(verbosity);
}