ifeq ($(call is-board-platform-in-list,trinket), false)
TARGET_USES_NON_LEGACY_POWERHAL := true
endif
