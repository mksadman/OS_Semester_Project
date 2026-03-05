# -*-Makefile-*-

OBJDIRS += $(KERN_OBJDIR)/flock

KERN_SRCFILES += $(KERN_DIR)/flock/flock.c

$(KERN_OBJDIR)/flock/%.o: $(KERN_DIR)/flock/%.c
	@echo + cc[KERN/flock] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(KERN_CFLAGS) -c -o $@ $<

$(KERN_OBJDIR)/flock/%.o: $(KERN_DIR)/flock/%.S
	@echo + as[KERN/flock] $<
	@mkdir -p $(@D)
	$(V)$(CC) $(KERN_CFLAGS) -c -o $@ $<