# Read the transaction cover VCD and check the four strict certificate reports.
BEGIN {
  low=(mode=="low")
  s=low?5:8; e=low?8:5; joined=low?11:10
  scale=low?"b6":"a7"; element=low?"57c6a19b83024ed5":"d16427ba91050ce3"
  bad["scale_inverse"]=s+1; bad["join_inverse"]=joined
  bad["scale_handshake"]=s; bad["transaction"]=joined
  tick=-1
}
function fail(msg) { print FILENAME ": " msg > "/dev/stderr"; failed=1; exit 1 }
ARGIND==1 {
  if($2=="witness" && $3=="loop") {
    name=substr($1,length(top)+2); sub(/:$/,"",name)
    if(!(name in bad) || seen[name]++ || $4+0 >= $6+0 || $11+0 != bad[name])
      fail("unexpected witness: " $0)
    if(name=="transaction") {head=$4+0; end=$6+0}
  }
  next
}
function hex(bits,   h,i,n,j,word) {
  if(bits=="" || bits ~ /[^01]/) fail("missing or unknown sample at " tick)
  while(length(bits)%4) bits="0" bits
  for(i=1;i<=length(bits);i+=4) {
    n=0; for(j=0;j<4;j++) n=n*2+substr(bits,i+j,1)
    word=word substr("0123456789abcdef",n+1,1)
  }
  sub(/^0+/,"",word); return word==""?"0":word
}
function check(name,want,bits,   got) {
  got=hex(bits)
  if(("v" got)!=("v" want)) fail(name " at " tick ": got " got ", expected " want)
  print tick "\t" name "\t" got
}
function value(name,want) { check(name,want,signal[name]) }
function sample(   d) {
  if(tick<0) return
  if(tick==0) value("rst","1")
  if(tick>=1 && tick<joined) value("prefetch_data_valid","0")
  if(tick==s) {
    d=signal["host_scale_d_i"]
    if(length(d)!=26) fail("scale D width")
    value("scale_d_accept","1")
    check("scale_opcode","1",substr(d,1,3))
    check("scale_payload",scale,substr(d,19))
  }
  if(tick==e) {
    d=signal["host_element_d_i"]
    if(length(d)!=82) fail("element D width")
    value("element_d_accept","1")
    check("element_payload",element,substr(d,19))
  }
  if(tick==s+1) {value("u_dut.loaded_scale_valid","1"); value("u_dut.loaded_scale",scale)}
  if(tick==s+2) {value("u_dut.prefetch_scale_data_valid","1"); value("prefetch_scale",scale)}
  if(tick==e+2) {value("active_loaded_valid","1"); value("active_loaded_data",element)}
  if(tick==e+3) {value("u_dut.prefetch_element_data_valid","1"); value("active_out",element)}
  if(tick==joined) {
    value("prefetch_data_valid","1"); value("prefetch_data_ready","1")
    value("prefetch_scale",scale); value("active_out",element); value("sampled_low_mask","ff")
  }
  # The two readers can choose different loop starts. Both must be on the
  # saturated schedule and include the repeated frame in the VCD.
  if(tick==head || tick==end)
    if(hex(signal["frame"])!="d") fail("transaction loop outside stable schedule")
}
/^#[0-9]+$/ { sample(); if(substr($0,2)+0!=++tick) fail("missing frame"); next }
/^b[01xz]+ / { id=$2; bits=substr($1,2) }
/^[01xz][^ ]+$/ { id=substr($0,2); bits=substr($0,1,1) }
{
  if(id!="" && index(id,top ".")==1) signal[substr(id,length(top)+2)]=bits
  id=""
}
END {
  if(failed) exit 1
  sample()
  if(length(seen)!=4 || tick!=end) fail("incomplete certificates or VCD suffix")
}
