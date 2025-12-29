#include "zheap.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static bool signal_barrier_enabled = false;

// Signal Handler for SIGSEGV
void zbarrier_signal_handler(int signum, siginfo_t *info, void *context) {
  void *addr = info->si_addr;
  // printf removed for signal safety

  // Check if address is within our heap
  // For this prototype, we just check if it's a ZPage
  ZPage *page = zheap_get_page(addr);

  if (page && page->is_evacuating) {
    // It's a trap on an evacuating page!
    // "Lazy Unprotect" Strategy:
    // Unprotect the page so the instruction can succeed.
    // The program will continue using the OLD object (in the From-Space).
    // This is safe because we haven't overwritten the From-Space yet.
    // However, we lose the barrier for this page (no more forwarding).

    // printf("[ZBarrier-Signal] Trap at %p! Unprotecting page %p\n", addr,
    // page);

    if (mprotect((void *)page->start, ZPAGE_SIZE, PROT_READ | PROT_WRITE) ==
        -1) {
      perror("mprotect failed in signal handler");
      abort();
    }

    // We successfully handled it. Resume execution.
    return;
  }

  // Not our fault? Re-raise or default handler?
  // For now, print and abort to be safe/loud.
  fprintf(stderr, "[ZBarrier-Signal] Segmentation Fault at %p (Not a ZPage)\n",
          addr);
  abort();
}

void zbarrier_enable_signal_mode(void) {
#ifdef __APPLE__
  printf("[ZBarrier-Signal] Signal-Based Barriers NOT SUPPORTED on macOS. "
         "Using software barriers.\n");
  return;
#endif

  if (signal_barrier_enabled)
    return;

  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = zbarrier_signal_handler;

  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    perror("sigaction SIGSEGV");
    return;
  }
  if (sigaction(SIGBUS, &sa, NULL) == -1) {
    perror("sigaction SIGBUS");
    return;
  }

  signal_barrier_enabled = true;
  printf(
      "[ZBarrier-Signal] Signal-Based Barriers ENABLED (SIGSEGV + SIGBUS)\n");
}

void zbarrier_disable_signal_mode(void) {
  if (!signal_barrier_enabled)
    return;

  // Restore default handler
  signal(SIGSEGV, SIG_DFL);
  signal_barrier_enabled = false;
  printf("[ZBarrier-Signal] Signal-Based Barriers DISABLED\n");
}

bool zbarrier_is_signal_mode(void) { return signal_barrier_enabled; }
