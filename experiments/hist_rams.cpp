class HistRAM {

  input_8 debug_addr;
  output_32 debug_data;

  input_8 debug_write_addr;
  input_32 debug_write_data;
  input_1 debug_write_en;  

  input_32 wdata_0;
  input_8 waddr_0;
  input_1 wen_0;

  input_8 raddr_0;
  output_32 rdata_0;

  void hwrite(bit_8& addr, bit_32& data) {
  set_wen:
    set_port(wen_0, 1);
  set_wdata:
    set_port(wdata_0, data);
  set_waddr:
    set_port(waddr_0, addr);

  ret: return;

    add_constraint(start(set_wen) == start(set_wdata));
    add_constraint(start(set_wen) == start(set_waddr));
    add_constraint(start(set_wen) + 1 == end(ret));
  }

  bit_32 hread(bit_8& addr) {
  set_addr:
    set_port(raddr_0, addr);

    bit_32 res;

  read_data:
    res = read_port(rdata_0);

    return res;

    add_constraint(end(set_addr) == start(read_data));
    add_constraint(start(read_data) == start(ret));
  }

};

class ImgRAM {

  input_12 debug_addr;
  output_8 debug_data;

  input_12 debug_write_addr;
  input_8 debug_write_data;
  input_1 debug_write_en;  

  input_8 wdata_0;
  input_12 waddr_0;
  input_1 wen_0;

  input_12 raddr_0;
  output_8 rdata_0;

  void write(bit_12& addr, bit_8& data) {
  set_wen:
    set_port(wen_0, 1);
  set_wdata:
    set_port(wdata_0, data);
  set_waddr:
    set_port(waddr_0, addr);

  ret: return;

    add_constraint(start(set_wen) == start(set_wdata));
    add_constraint(start(set_wen) == start(set_waddr));
    add_constraint(start(set_wen) == end(ret));
  }

  bit_8 read(bit_12& addr) {
  set_addr:
    set_port(raddr_0, addr);

    bit_8 res;

  read_data:
    res = read_port(rdata_0);

    return res;

    add_constraint(end(set_addr) == start(read_data));
    add_constraint(start(read_data) == start(ret));
  }

};

void histogram(ImgRAM& img, HistRAM& hist) {
  bit_8 i;
  bit_8 pix;
  bit_32 count;
  i = 0;
  do {
    pix = img.read(i);
    count = hist.hread(pix);
    count = count + 1;
    hist.hwrite(pix, count);
    i = i + 1;
  } while (i < 100);
}
