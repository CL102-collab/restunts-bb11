"""Allow slower instrumented builds to finish fixed regression workloads."""

import os
import re


TIMEOUT_MULTIPLIER_VARIABLE = "RESTUNTS_GENGHOST_TEST_TIMEOUT_MULTIPLIER"


def scaled_timeout(seconds):
    """Scale computational timeouts while preserving normal-build defaults."""
    multiplier = os.environ.get(TIMEOUT_MULTIPLIER_VARIABLE, "1")
    if re.fullmatch(r"[1-9][0-9]*", multiplier) is None:
        raise ValueError(f"{TIMEOUT_MULTIPLIER_VARIABLE} must be a positive integer")
    return seconds * int(multiplier)
