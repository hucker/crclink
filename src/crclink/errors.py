"""Error types for crclink."""


class CrclinkError(ValueError):
    """Base exception for crclink failures."""


class FrameFormatError(CrclinkError):
    """Raised when input frame structure is malformed."""


class CrcMismatchError(CrclinkError):
    """Raised when computed CRC does not match claimed CRC."""
