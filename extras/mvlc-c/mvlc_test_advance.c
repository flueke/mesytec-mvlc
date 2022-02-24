#include <assert.h>
#include <mesytec-mvlc-c.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#define errbufsize 1024u

char errbuf[errbufsize];


int tdiff_usecs(struct timeval t2, struct timeval t1)
{
     return (t2.tv_sec-t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
}
struct timeval t_start, t_end;

// https://stackoverflow.com/a/64896093/17562886
int64_t difftimespec_us(const struct timespec after, const struct timespec before)
{
    return ((int64_t)after.tv_sec - (int64_t)before.tv_sec) * (int64_t)1000000
         + ((int64_t)after.tv_nsec - (int64_t)before.tv_nsec) / 1000;
}

void print_buffer(u32 *data, size_t size)
{
  printf("Begin buffer of size %lu:\n", size);
  for (size_t i=0; i<size; ++i)
  {
    printf("  0x%08X\n", data[i]);
  }
  printf("End buffer of size %lu:\n", size);
}


int main(int argc, char *argv[])
{
  const char *mvlcHost = "10.0.0.22";
  printf("Trying to open mesytec on ip 10.0.0.22\n");
  //mvlc_ctrl_t *mvlc = mvlc_ctrl_create_eth("10.0.0.22");
  mvlc_ctrl_t *mvlc = mvlc_ctrl_create_eth("MVLC-0066");

  // Tell the MVLC to disable all triggers upon connecting. Useful if the
  // program aborted earlier and left DAQ mode enabled.
  mvlc_ctrl_set_disable_trigger_on_connect(mvlc, true);

  mvlc_err_t err = mvlc_ctrl_connect(mvlc);

  if (!mvlc_is_error(err))
    {
      printf("Connected to MVLC_ETH!\n");
    }
  else
    {
      printf("Error connecting to MVLC_ETH: %s\n", mvlc_format_error(err, errbuf, errbufsize));
      return 1;
    }

  printf("Running a few of the MVLC functions..\n");

  printf("\tisConnected: %d\n", mvlc_ctrl_is_connected(mvlc));
  printf("\thardwareId: 0x%04x\n", get_mvlc_ctrl_hardware_id(mvlc));
  printf("\tfirmwareRev: 0x%04x\n", get_mvlc_ctrl_firmware_revision(mvlc));


  u32 modBase = 0x09000000;
  u32 fwReg = 0x0008;
  // amod 0x08: MBLT64
  // amod 0x09: a32UserData
  u8 amod = 0x09;

  printf("Initializing VME module\n");

  // Minimal initialization for a mesytec module located at the modBase address.
  // Module reset + sleep afterwards
  err = mvlc_ctrl_vme_write(mvlc, modBase + 0x6008, 1, amod, MVLC_VMEDataWidth_D16);
  assert(!mvlc_is_error(err));
  sleep(1);

  // signal IRQ 1
  err = mvlc_ctrl_vme_write(mvlc, modBase + 0x6010, 1, amod, MVLC_VMEDataWidth_D16);
  assert(!mvlc_is_error(err));
  // single event mode, module buffering disabled
  err = mvlc_ctrl_vme_write(mvlc, modBase + 0x6038, 0, amod, MVLC_VMEDataWidth_D16);
  assert(!mvlc_is_error(err));
  // Enable the test pulser (the exact value needed depends on the module type, mdpp16:1, mtdc32:3, ...).
  err = mvlc_ctrl_vme_write(mvlc, modBase + 0x6070, 1, amod, MVLC_VMEDataWidth_D16);
  assert(!mvlc_is_error(err));


  mvlc_stackbuilder_t *stack = mvlc_stackbuilder_create("readout_test");
  //mvlc_stackbuilder_add_vme_read(stack, modBase + fwReg, amod,  MVLC_VMEDataWidth_D32_slow);

  // MBLT FIFO read from the module base address
  mvlc_stackbuilder_add_vme_block_read(stack, modBase, 0x08, 65535);
  // For mesytec modules: write the "readout reset" register.
  mvlc_stackbuilder_add_vme_write(stack, modBase + 0x6034, 1, amod, MVLC_VMEDataWidth_D16);

  // Upload the stack

  printf("Setting up readout stack\n");

  u16 stackUploadOffset = 1;
  err = mvlc_upload_stack(mvlc, MVLC_DataPipe, stackUploadOffset, stack);

  if (!mvlc_is_error(err))
    printf("Command stack uploaded.\n");
  else
  {
    printf("Error uploading command stack: %s\n", mvlc_format_error(err, errbuf, errbufsize));
    return 1;
  }

  // Can now release the memory used by the stack builder.
  mvlc_stackbuilder_destroy(stack);

  // Now setup the stack offset and trigger registers.

  u8 stackId = 1; // first readout stack
  u8 triggerIRQ = 1; // react to IRQ1

  u16 stackOffsetRegister = mvlc_get_stack_offset_register(stackId);
  u16 stackTriggerRegister = mvlc_get_stack_trigger_register(stackId);
  u16 triggerValue = mvlc_calculate_trigger_value(StackTrigger_IRQWithIACK, triggerIRQ);

  err = mvlc_ctrl_write_register(mvlc, stackOffsetRegister, stackUploadOffset);
  assert(!mvlc_is_error(err));

  err = mvlc_ctrl_write_register(mvlc, stackTriggerRegister, triggerValue);
  assert(!mvlc_is_error(err));

  // Calculate the start address for the next stack in case there are more to upload.
  //u16 nextStackUploadOffset = stackUploadOffset + get_stack_size_in_words(stack) + 1;

  printf("Enabling MVLC DAQ mode\n");
  err = mvlc_set_daq_mode(mvlc, 1);
  assert(!mvlc_is_error(err));

  // Low-level readout loop
  // Ethernet only: get the data socket and start low-level reads
  int dataSocket = mvlc_ctrl_eth_get_data_socket(mvlc);
  assert(dataSocket >= 0);

  const int secondsToRun = 10;
  size_t dataPacketsReceived = 0u;
  struct timespec tStart, tNow;
  clock_gettime(CLOCK_MONOTONIC, &tStart);

  while (true)
  {
    clock_gettime(CLOCK_MONOTONIC, &tNow);

    if (difftimespec_us(tNow, tStart) > secondsToRun * 1000 * 1000)
      break;

    u8 packetBuffer[1500];
    ssize_t bytesRead = recv(dataSocket, packetBuffer, sizeof(packetBuffer), 0);

    if (bytesRead > 0)
    {
      ++dataPacketsReceived;
      print_buffer((u32 *) packetBuffer, bytesRead / sizeof(u32));
    }
  }

  printf("Disabling MVLC DAQ mode\n");
  err = mvlc_set_daq_mode(mvlc, 0);
  assert(!mvlc_is_error(err));

  printf("Received %lu data packets from MVLC\n", dataPacketsReceived);


  //mvlc_crateconfig_t *config = mvlc_read_crateconfig_from_file("read_vulom_script.txt");
  printf("Disconnecting from MVLC_ETH..\n");
  err = mvlc_ctrl_disconnect(mvlc);

  if (!mvlc_is_error(err))
    {
      printf("Disconnected from MVLC_ETH.\n");
    }
  else
    {
      printf("Error disconnecting from MVLC_ETH: %s\n", mvlc_format_error(err, errbuf, errbufsize));
    }

  mvlc_ctrl_destroy(mvlc);

   return 0;
}

/**\}*/
