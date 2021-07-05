# Generate certificates tools.

## Generate certificates & Concatenate certificates.

* usage

    ```
    Usage: gen_certs_all.sh [OPTION]...
    Generate certificates & Concatenate certificates.
    
      OPTION:
        -o OUTPUT_DIR              Output dir. (default: /home/hoge/dev-env/tools/gen_certificate/out)
        -n INTER_CA_NUM            Number of Intermediate CA.
                                   (INTER_CA_NUM <= 10)
                                   (default: 3)
        -s ISSUER_CA_SERVER_CERT   Issuer CA of Server certificate.
                                   Specify suffix of Intermediate CA.
                                   (default: 2)
        -c ISSUER_CA_CLIENT_CERT   Issuer CA of Client certificate.
                                   Specify suffix of Intermediate CA.
                                   (default: 3)
        -S SUBJECT_SERVER_CERT     Subject of Server certificate.
                                   Last subject must be CN.
                                   (default: /C=JP/ST=Tokyo/O=SRA/CN=gfarm_server${SUBJECT_SUFFIX}.sra.co.jp)
        -C SUBJECT_CLIEN_CERT      Subject of Client certificate.
                                   Last subject must be CN.
                                   (default: /C=JP/ST=Tokyo/O=SRA/CN=gfarm_client${SUBJECT_SUFFIX}.sra.co.jp)
        -X SUBJECT_SUFFIX          Suffix of subject. (default: 1)
        -P                         Enable interactive password input.
                                   Only the client private key password is valid.
                                   If you do not specify this option, the password is "test".
                                   (default: FALSE)
        -h                         Help.
    ```

* Example of use

    ```
    % ./gen_certs_all.sh
    
    # Case where you want to change the subject of
    # Server certificate and Client certificate.
    # default: Server certificate: /C=JP/ST=Tokyo/O=SRA/CN=gfarm_server1.sra.co.jp
    #          Client certificate: /C=JP/ST=Tokyo/O=SRA/CN=gfarm_client1.sra.co.jp
    % ./gen_certs_all.sh -S "/C=JP/ST=Tokyo/..." -C "/C=JP/ST=Tokyo/..."
    
    # Case of specifying the password of the client's encrypted private key.
    # default: test
    % ./gen_certs_all.sh -P
    Password of client private key: << Input password
    ```

* output dirs

    ```
    # CAs.
    % ls  out/cas
    client  inter_ca_1  inter_ca_2  inter_ca_3  root_ca  server
    
    # root certificate、all intermediate certificates.
    % ls out/cacerts_all/
    41911cec.0  4784710c.0  4fb34984.0  b8df251b.0  inter_ca_1.crt  inter_ca_2.crt  inter_ca_3.crt  root_ca.crt
    
    # root certificate only.
    % ls out/cacerts_root/
    41911cec.0  root_ca.crt
    
    # server certificate
    # server_cat_all.crt is all concatenated.
    % ls out/server/
    server.crt  server.key  server_cat_all.crt
    
    # client certificate
    # client_cat_all.crt is all concatenated.
    # client_encrypted.key is the encrypted private key.
    % ls out/client/
    client.crt  client.key  client_cat_all.crt  client_encrypted.key
    ```

## Other tools
These are run through `gen_certs_all.sh`.

### Generate certificate.

    ```
    Usage: gen_cert.sh [OPTION]...
    Generate a certificate.
    
      OPTION:
        -r                   Generate Root certificate. (default)
        -i                   Generate Intermediate certificate. (Required opts: -x, -I)
        -s                   Generate Server certificate. (Required opts: -I)
        -c                   Generate Client certificate. (Required opts: -I)
        -d DAYS              Expiration. (default: 36500)
        -o OUTPUT_DIR        Output dir. (default: /home/vagrant/work/dev-env/tools/gen_certificate/out)
        -p PASS              Password for client private key.
                             Only the client private key password is valid.
                             If you do not specify this option, the password is "test".
        -P                   Enable interactive password input.
                             Only the client private key password is valid.
                             If you do not specify this option, the password is "test".
                             (default: FALSE)
        -x INTER_CA_SUFFIX   Suffix of Intermediate CA. (default: empty)
        -X SUBJECT_SUFFIX    Suffix of subject. (default: 1)
        -I ISSUER_CA         Issuer CA. (default: empty)
        -S SUBJ              Subject.
                             Last subject of the server certificate and client certificate must be CN.
        -h                   Help.
    ```

### Generate a hash file of certificate.

    ```
    % ./gen_hash_cert_files.sh -h
    Usage: gen_hash_cert_files.sh [OPTION]... CERTS_DIR
    Generate a hash file of certificate.
    
      CERTS_DIR:     Certificates directory.
    
      OPTION:
        -h           Help.
    ```

### Concatenate certificates.

    ```
    % ./cat_cert.sh -h
    Usage: cat_cert.sh [OPTION]... CERTS...
    Concatenate certificates.
    
      CERTS:               List of certificates to concat.
                           Specify in the order of concatenation toward Root certificate.
    
      OPTION:
        -o OUTPUT_FILE     Output file (default: ./cat.crt)
        -h                 Help.
    ```
