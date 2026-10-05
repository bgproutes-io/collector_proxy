import argparse
import ipaddress
import requests
import signal
import socket
import os
import re
import subprocess
import tempfile
import time


CERTIFICATE_RENEWAL_THRESHOLD_SECONDS = 7 * 24 * 60 * 60
CERTIFICATE_CHECK_INTERVAL_SECONDS = 24 * 60 * 60
CERTIFICATE_RENEWAL_RETRY_DELAYS_SECONDS = (5 * 60, 15 * 60, 60 * 60)
CERTIFICATE_RENEWAL_MAX_RETRY_DELAY_SECONDS = 6 * 60 * 60


class Config:
    def __init__(self, filename :str):
        self.filename = filename

        self.collector_ip :str = None
        self.collector_id :str = None
        self.cert_token   :str = None
        self.internal_directory :str = None
        self.collector_port :int = None
        self.use_tls : bool = None
        self.command_host :str = None
        self.command_port :int = None

        if not os.path.exists(filename):
            print(f"Configuration file {filename} cannot be found locally.")
            exit(1)

        with open(filename, 'r') as f:
            line_idx = 1

            for line in f:
                stripped_line = line.strip()
                if not stripped_line or stripped_line.startswith("#"):
                    continue

                keyword, separator, value = stripped_line.partition(":")
                if not separator:
                    print(f"Invalid configuration at line {line_idx}")
                    line_idx += 1
                    continue

                keyword = keyword.strip()
                value = value.strip()

                if not hasattr(self, keyword):
                    print(f"Configuration does not have a value '{keyword}' (line {line_idx}). Skiping.")
                    line_idx += 1
                    continue

                setattr(self, keyword, value)
                line_idx += 1

        enrollment_token = os.environ.get("BMP_TLS_ENROLLMENT_TOKEN")
        if enrollment_token:
            self.cert_token = enrollment_token

        try:
            self.collector_port = int(self.collector_port)
            self.command_port   = int(self.command_port)
            normalized_use_tls = self.use_tls.strip().lower()
            if normalized_use_tls in ("1", "true", "yes", "on"):
                self.use_tls = True
            elif normalized_use_tls in ("0", "false", "no", "off"):
                self.use_tls = False
            else:
                raise ValueError("use_tls must be a boolean value")
        except (AttributeError, TypeError, ValueError) as error:
            raise ValueError(
                "collector_port, command_port, and use_tls must be configured"
            ) from error



class CertGenerator:
    def __init__(self, cfg_file :str, notify_pid_file=None):
        self.config = Config(cfg_file)
        self.notify_pid_file = notify_pid_file

        ### --- We do not need to use TLS, just exit this process --- ###
        if not self.config.use_tls:
            exit(0)



    def get_signer_url(self):
        try:
            hostname, aliases, addresses = socket.gethostbyaddr(self.config.collector_ip)
            return hostname
        except (socket.herror, socket.gaierror):
            return None



    def generate_first_cert(self):
        signer_url = self.get_signer_url()

        if signer_url is None:
            raise RuntimeError(
                f"Unable to resolve signer hostname from "
                f"{self.config.collector_ip}"
            )

        # If get_signer_url() only returns the hostname:
        signer_url = f"https://{signer_url}"

        cert_dir = self.config.internal_directory

        if not cert_dir:
            raise RuntimeError("internal_directory is not configured")

        os.makedirs(cert_dir, exist_ok=True)

        key_path = os.path.join(cert_dir, "client.key")
        csr_path = os.path.join(cert_dir, "client.csr")
        cert_path = os.path.join(cert_dir, "client.crt")
        ca_path = os.path.join(cert_dir, "ca.crt")

        #
        # 1. Generate RSA 3072-bit private key
        #
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm", "RSA",
                "-pkeyopt", "rsa_keygen_bits:3072",
                "-out", key_path,
            ],
            check=True,
        )

        # Protect the private key.
        os.chmod(key_path, 0o600)

        #
        # 2. Generate CSR
        #
        subject = (
            f"/O=BGPRoutes"
            f"/OU=BMP Proxies"
            f"/CN={self.config.collector_id}"
        )

        subprocess.run(
            [
                "openssl",
                "req",
                "-new",
                "-key", key_path,
                "-out", csr_path,
                "-subj", subject,
            ],
            check=True,
        )

        #
        # 3. Verify CSR signature
        #
        subprocess.run(
            [
                "openssl",
                "req",
                "-in", csr_path,
                "-noout",
                "-verify",
            ],
            check=True,
        )

        #
        # 4. Read CSR
        #
        with open(csr_path, "r") as f:
            csr_pem = f.read()

        #
        # 5. Equivalent of sign-request.json
        #
        payload = {
            "csr_pem": csr_pem,
            "collector_id": self.config.collector_id,
            "collector_ip": self.config.collector_ip,
        }

        #
        # 6. POST CSR to certificate signer
        #
        response = requests.post(
            f"{signer_url}/sign",
            headers={
                "Authorization": f"Bearer {self.config.cert_token}",
            },
            json=payload,
            timeout=30,
        )

        # Equivalent to curl --fail-with-body
        try:
            response.raise_for_status()
        except requests.HTTPError:
            raise RuntimeError(
                f"Certificate signer returned HTTP "
                f"{response.status_code}:\n{response.text}"
            )

        #
        # 7. Parse response
        #
        try:
            response_data = response.json()

            certificate_pem = response_data["certificate_pem"]
            ca_certificate_pem = response_data["ca_certificate_pem"]

        except (ValueError, KeyError) as e:
            raise RuntimeError(
                f"Invalid response from certificate signer: "
                f"{response.text}"
            ) from e

        #
        # 8. Save certificates
        #
        with open(cert_path, "w") as f:
            f.write(certificate_pem)

        with open(ca_path, "w") as f:
            f.write(ca_certificate_pem)

        os.chmod(cert_path, 0o644)
        os.chmod(ca_path, 0o644)
        os.chmod(csr_path, 0o644)

        #
        # 9. Verify issued certificate against CA
        #
        subprocess.run(
            [
                "openssl",
                "verify",
                "-purpose", "sslclient",
                "-CAfile", ca_path,
                cert_path,
            ],
            check=True,
        )

        #
        # 10. Inspect certificate
        #
        result = subprocess.run(
            [
                "openssl",
                "x509",
                "-in", cert_path,
                "-noout",
                "-subject",
                "-issuer",
                "-dates",
                "-ext", "extendedKeyUsage",
                "-ext", "subjectAltName",
            ],
            check=True,
            text=True,
            capture_output=True,
        )

        return cert_path, key_path, ca_path



    def renew_cert(self):
        """Renew the client certificate using the existing certificate as proof.

        The enrollment token is deliberately not sent to the renewal endpoint.
        The signer authorizes the request by checking that the current certificate
        is valid and that the CSR was made with the same private key.
        """
        signer_hostname = self.get_signer_url()
        if signer_hostname is None:
            raise RuntimeError(
                f"Unable to resolve signer hostname from "
                f"{self.config.collector_ip}"
            )

        cert_dir = self.config.internal_directory
        if not cert_dir:
            raise RuntimeError("internal_directory is not configured")

        key_path = os.path.join(cert_dir, "client.key")
        csr_path = os.path.join(cert_dir, "client.csr")
        cert_path = os.path.join(cert_dir, "client.crt")
        ca_path = os.path.join(cert_dir, "ca.crt")

        for path, description in (
            (key_path, "client private key"),
            (cert_path, "current client certificate"),
        ):
            if not os.path.isfile(path):
                raise RuntimeError(f"Missing {description}: {path}")

        # The backend requires proof of possession of the current private key.
        # Therefore renewal reuses client.key and creates a fresh CSR from it.
        temporary_csr_fd, temporary_csr_path = tempfile.mkstemp(
            prefix=".client.csr.", dir=cert_dir
        )
        os.close(temporary_csr_fd)

        temporary_cert_path = None
        temporary_ca_path = None
        try:
            subject = (
                f"/O=BGPRoutes"
                f"/OU=BMP Proxies"
                f"/CN={self.config.collector_id}"
            )
            subprocess.run(
                [
                    "openssl", "req", "-new", "-sha256",
                    "-key", key_path,
                    "-out", temporary_csr_path,
                    "-subj", subject,
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            subprocess.run(
                [
                    "openssl", "req", "-in", temporary_csr_path,
                    "-noout", "-verify",
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            with open(temporary_csr_path, "r", encoding="ascii") as csr_file:
                csr_pem = csr_file.read()
            with open(cert_path, "r", encoding="ascii") as certificate_file:
                current_certificate_pem = certificate_file.read()

            response = requests.post(
                f"https://{signer_hostname}/renew",
                json={
                    "csr_pem": csr_pem,
                    "current_certificate_pem": current_certificate_pem,
                    "collector_id": self.config.collector_id,
                    "collector_ip": self.config.collector_ip,
                    "certificate_profile": "client",
                },
                timeout=(3.05, 15),
            )
            try:
                response.raise_for_status()
            except requests.HTTPError as error:
                raise RuntimeError(
                    f"Certificate signer returned HTTP "
                    f"{response.status_code}:\n{response.text}"
                ) from error

            try:
                response_data = response.json()
                certificate_pem = response_data["certificate_pem"]
                ca_certificate_pem = response_data["ca_certificate_pem"]
            except (ValueError, KeyError, TypeError) as error:
                raise RuntimeError(
                    f"Invalid response from certificate signer: {response.text}"
                ) from error

            if (
                not isinstance(certificate_pem, str)
                or not certificate_pem.startswith("-----BEGIN CERTIFICATE-----")
                or not isinstance(ca_certificate_pem, str)
                or not ca_certificate_pem.startswith("-----BEGIN CERTIFICATE-----")
            ):
                raise RuntimeError("Certificate signer returned invalid PEM data")

            temporary_cert_fd, temporary_cert_path = tempfile.mkstemp(
                prefix=".client.crt.", dir=cert_dir
            )
            temporary_ca_fd, temporary_ca_path = tempfile.mkstemp(
                prefix=".ca.crt.", dir=cert_dir
            )
            with os.fdopen(temporary_cert_fd, "w", encoding="ascii") as cert_file:
                cert_file.write(certificate_pem)
                cert_file.flush()
                os.fsync(cert_file.fileno())
            with os.fdopen(temporary_ca_fd, "w", encoding="ascii") as ca_file:
                ca_file.write(ca_certificate_pem)
                ca_file.flush()
                os.fsync(ca_file.fileno())

            self._validate_renewed_client_certificate(
                key_path, temporary_cert_path, temporary_ca_path
            )

            os.chmod(temporary_cert_path, 0o644)
            os.chmod(temporary_ca_path, 0o644)
            os.chmod(temporary_csr_path, 0o644)

            # Keep the previous usable certificate in place until all validation
            # has succeeded, then atomically replace the public files.
            os.replace(temporary_ca_path, ca_path)
            temporary_ca_path = None
            os.replace(temporary_cert_path, cert_path)
            temporary_cert_path = None
            os.replace(temporary_csr_path, csr_path)

            self._notify_proxy_reload()

            return cert_path, key_path, ca_path
        finally:
            for temporary_path in (
                temporary_csr_path,
                temporary_cert_path,
                temporary_ca_path,
            ):
                if temporary_path and os.path.exists(temporary_path):
                    os.unlink(temporary_path)



    def certificate_needs_renewal(self):
        """Return True when client.crt expires in seven days or less."""
        cert_dir = self.config.internal_directory
        if not cert_dir:
            raise RuntimeError("internal_directory is not configured")

        cert_path = os.path.join(cert_dir, "client.crt")
        if not os.path.isfile(cert_path):
            raise RuntimeError(f"Missing client certificate: {cert_path}")

        # Parse the certificate separately so a malformed certificate is not
        # mistaken for a valid certificate that merely needs renewal.
        subprocess.run(
            ["openssl", "x509", "-in", cert_path, "-noout"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        result = subprocess.run(
            [
                "openssl", "x509", "-in", cert_path,
                "-noout", "-checkend",
                str(CERTIFICATE_RENEWAL_THRESHOLD_SECONDS),
            ],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return result.returncode != 0



    def validate_existing_certificate(self):
        """Validate persisted certificates without using the enrollment token."""
        cert_dir = self.config.internal_directory
        if not cert_dir:
            raise RuntimeError("internal_directory is not configured")

        self._validate_client_certificate(
            os.path.join(cert_dir, "client.key"),
            os.path.join(cert_dir, "client.crt"),
            os.path.join(cert_dir, "ca.crt"),
            minimum_validity_seconds=0,
        )



    @staticmethod
    def _renewal_retry_delay(consecutive_failures):
        if consecutive_failures <= 0:
            raise ValueError("consecutive_failures must be positive")
        retry_index = consecutive_failures - 1
        if retry_index < len(CERTIFICATE_RENEWAL_RETRY_DELAYS_SECONDS):
            return CERTIFICATE_RENEWAL_RETRY_DELAYS_SECONDS[retry_index]
        return CERTIFICATE_RENEWAL_MAX_RETRY_DELAY_SECONDS



    def run_renewal_loop(self):
        """Check certificates daily and listen for UDP renewal commands.

        Sending the exact ASCII command ``reload-certs`` forces an immediate
        renewal, independently of the certificate's remaining lifetime.
        """
        try:
            command_port = int(self.config.command_port)
        except (TypeError, ValueError) as error:
            raise ValueError("command_port must be an integer") from error
        if not 1 <= command_port <= 65535:
            raise ValueError("command_port must be between 1 and 65535")

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as command_socket:
            command_socket.bind((self.config.command_host, command_port))
            next_certificate_check = time.monotonic()
            consecutive_renewal_failures = 0

            while True:
                current_time = time.monotonic()
                if current_time >= next_certificate_check:
                    if self._renew_if_needed():
                        consecutive_renewal_failures = 0
                        next_certificate_check = (
                            time.monotonic() + CERTIFICATE_CHECK_INTERVAL_SECONDS
                        )
                    else:
                        consecutive_renewal_failures += 1
                        retry_delay = self._renewal_retry_delay(
                            consecutive_renewal_failures
                        )
                        next_certificate_check = time.monotonic() + retry_delay
                        print(
                            "Retrying certificate renewal check in "
                            f"{retry_delay} seconds"
                        )
                    continue

                command_socket.settimeout(next_certificate_check - current_time)
                try:
                    command_data, sender = command_socket.recvfrom(1024)
                except socket.timeout:
                    continue

                try:
                    command = command_data.decode("ascii", errors="strict").strip()
                except UnicodeDecodeError:
                    self._send_command_response(
                        command_socket, sender, "error: command must be ASCII"
                    )
                    continue

                if command != "reload-certs":
                    self._send_command_response(
                        command_socket, sender, "error: unknown command"
                    )
                    continue

                try:
                    self.renew_cert()
                except Exception as error:
                    print(f"Certificate renewal command failed: {error}")
                    consecutive_renewal_failures += 1
                    retry_delay = self._renewal_retry_delay(
                        consecutive_renewal_failures
                    )
                    next_certificate_check = min(
                        next_certificate_check,
                        time.monotonic() + retry_delay,
                    )
                    self._send_command_response(
                        command_socket, sender, "error: certificate renewal failed"
                    )
                else:
                    consecutive_renewal_failures = 0
                    next_certificate_check = (
                        time.monotonic() + CERTIFICATE_CHECK_INTERVAL_SECONDS
                    )
                    self._send_command_response(
                        command_socket, sender, "ok: certificate renewed"
                    )



    def _renew_if_needed(self):
        try:
            if self.certificate_needs_renewal():
                self.renew_cert()
            return True
        except Exception as error:
            # A temporary signer or network failure must not permanently
            # disable future renewal attempts.
            print(f"Certificate renewal check failed: {error}")
            return False



    @staticmethod
    def _send_command_response(command_socket, sender, message):
        try:
            command_socket.sendto(message.encode("ascii"), sender)
        except OSError as error:
            print(f"Unable to send certificate command response: {error}")



    def _notify_proxy_reload(self):
        if not self.notify_pid_file:
            return

        try:
            with open(self.notify_pid_file, "r", encoding="ascii") as pid_file:
                proxy_pid = int(pid_file.read().strip())
            if proxy_pid <= 1:
                raise ValueError("invalid proxy PID")
            os.kill(proxy_pid, signal.SIGHUP)
            print(f"Notified proxy process {proxy_pid} to reload TLS certificates")
        except (OSError, TypeError, ValueError) as error:
            # Renewal itself succeeded and the files are safely installed. A
            # later proxy restart will still load them, so notification failure
            # must not turn the renewal into a false failure.
            print(f"Unable to notify proxy about renewed certificates: {error}")



    def _validate_client_certificate(
        self, key_path, certificate_path, ca_path,
        minimum_validity_seconds=CERTIFICATE_RENEWAL_THRESHOLD_SECONDS,
    ):
        subprocess.run(
            [
                "openssl", "verify", "-purpose", "sslclient",
                "-CAfile", ca_path, certificate_path,
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        subprocess.run(
            [
                "openssl", "x509", "-in", certificate_path,
                "-noout", "-checkend", str(minimum_validity_seconds),
            ],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        key_public = subprocess.run(
            ["openssl", "pkey", "-in", key_path, "-pubout"],
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
        certificate_public = subprocess.run(
            ["openssl", "x509", "-in", certificate_path, "-pubkey", "-noout"],
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
        if key_public != certificate_public:
            raise RuntimeError(
                "Signer returned a certificate that does not match client.key"
            )

        certificate_subject = subprocess.run(
            [
                "openssl", "x509", "-in", certificate_path,
                "-noout", "-subject", "-nameopt", "RFC2253",
            ],
            check=True,
            text=True,
            capture_output=True,
        ).stdout.strip()
        expected_subject = (
            f"subject=CN={self.config.collector_id},"
            f"OU=BMP Proxies,O=BGPRoutes"
        )
        if certificate_subject != expected_subject:
            raise RuntimeError("Signer returned a certificate with an unexpected subject")

        san_output = subprocess.run(
            [
                "openssl", "x509", "-in", certificate_path,
                "-noout", "-ext", "subjectAltName",
            ],
            check=True,
            text=True,
            capture_output=True,
        ).stdout
        expected_collector_uri = (
            f"URI:urn:bgproutes:collector:{self.config.collector_id}"
        )
        if expected_collector_uri not in san_output:
            raise RuntimeError(
                "Signer returned a certificate without the collector URI SAN"
            )
        if re.search(
            r"URI:urn:bgproutes:user-sha256:[0-9a-f]{64}", san_output
        ) is None:
            raise RuntimeError(
                "Signer returned a client certificate without the user identity SAN"
            )

        eku_output = subprocess.run(
            [
                "openssl", "x509", "-in", certificate_path,
                "-noout", "-ext", "extendedKeyUsage",
            ],
            check=True,
            text=True,
            capture_output=True,
        ).stdout
        if "TLS Web Client Authentication" not in eku_output:
            raise RuntimeError(
                "Signer returned a certificate without clientAuth EKU"
            )



    def _validate_renewed_client_certificate(
        self, key_path, certificate_path, ca_path
    ):
        self._validate_client_certificate(
            key_path,
            certificate_path,
            ca_path,
            minimum_validity_seconds=30 * 24 * 60 * 60,
        )



def main():
    parser = argparse.ArgumentParser(
        description="Generate and renew the BMP proxy TLS certificate"
    )
    parser.add_argument(
        "config_file",
        nargs="?",
        default="proxy.conf",
        help="configuration file to use (default: proxy.conf)",
    )
    parser.add_argument(
        "--bootstrap-only",
        action="store_true",
        help="prepare and validate certificates, then exit",
    )
    parser.add_argument(
        "--notify-pid-file",
        help="send SIGHUP to the PID stored in this file after renewal",
    )
    arguments = parser.parse_args()

    certificate_generator = CertGenerator(
        arguments.config_file,
        notify_pid_file=arguments.notify_pid_file,
    )
    cert_dir = certificate_generator.config.internal_directory
    if not cert_dir:
        raise RuntimeError("internal_directory is not configured")

    certificate_paths = (
        os.path.join(cert_dir, "client.key"),
        os.path.join(cert_dir, "client.crt"),
        os.path.join(cert_dir, "ca.crt"),
    )
    existing_paths = [os.path.isfile(path) for path in certificate_paths]

    if not any(existing_paths):
        certificate_generator.generate_first_cert()
    elif not all(existing_paths):
        missing_paths = [
            path for path, exists in zip(certificate_paths, existing_paths)
            if not exists
        ]
        raise RuntimeError(
            "Incomplete certificate state; missing: " + ", ".join(missing_paths)
        )
    else:
        # Reuse a complete certificate set from the persistent Docker volume.
        # This validation path never sends the one-time enrollment token.
        certificate_generator.validate_existing_certificate()

    if arguments.bootstrap_only:
        return

    certificate_generator.run_renewal_loop()



if __name__ == "__main__":
    main()
