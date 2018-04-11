// Remember to free strings returned by this.
static inline char *portable_userHomeDirectory(void) {
	char *homeDir = NULL;

#if defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)
	// Unix: First check the environment for $HOME.
	char *homeVar = getenv("HOME");

	if (homeVar != NULL) {
		homeDir = strdup(homeVar);
	}

	// Else try to get it from the user data storage.
	if (homeDir == NULL) {
		struct passwd userPasswd;
		struct passwd *userPasswdPtr;
		char userPasswdBuf[2048];

		if (getpwuid_r(getuid(), &userPasswd, userPasswdBuf, sizeof(userPasswdBuf), &userPasswdPtr) == 0) {
			homeDir = strdup(userPasswd.pw_dir);
		}
	}

	if (homeDir == NULL) {
		// Else just return /tmp as a place to write to.
		homeDir = strdup("/tmp");
	}
#elif defined(_WIN32)
	// Windows:
#endif

	// Check if anything worked.
	if (homeDir == NULL) {
		return (NULL);
	}

	char *realHomeDir = portable_realpath(homeDir);
	if (realHomeDir == NULL) {
		free(homeDir);

		return (NULL);
	}

	free(homeDir);

	return (realHomeDir);
}
