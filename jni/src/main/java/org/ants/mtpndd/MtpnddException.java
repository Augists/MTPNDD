package org.ants.mtpndd;

/**
 * Unchecked exception thrown when the underlying MTPNDD native runtime
 * reports an error.
 */
public class MtpnddException extends RuntimeException {
    public MtpnddException(String message) {
        super(message);
    }

    public MtpnddException(String message, Throwable cause) {
        super(message, cause);
    }
}
