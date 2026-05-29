XOAUTH2 Authentication Test

This directory contains automated test scripts for
Gfarm XOAUTH2 authentication.


Test Assumptions
----------------
The current test cases were created and validated with:

    - docker/dev environment
    - distribution: rockylinux8/pkg
    - default docker/dev issuer
    - default docker/dev hostname configuration
    - sasl_user: user1
    - JWT obtained by jwt-agent

Many test cases in tests.table assume
the issuer URL and JWT contents generated
by the above environment.


Files
-----
run_test.sh
    Main test execution script

tests.table
    Test case definitions

init_gfarm_sasl.conf
    Base configuration template

deploy_gfarm_sasl.sh
    Configuration deployment script

gfarm_sasl.conf
    Generated test configuration file


How to Run
----------
Obtain a JWT token before executing the tests.

Example:

    % jwt-agent -s https://jwt-server/ -l user1

    % ./run_test.sh


Test Flow
---------
1. Copy init_gfarm_sasl.conf
2. Apply configuration changes from tests.table
3. Execute deploy_gfarm_sasl.sh
4. Restart Gfarm services
5. Execute gfhost -l
6. Validate authentication result


Test Logs
---------
Logs are stored under:

    auth_xoauth2_test/

Example:

    auth_xoauth2_test/test_3/
                        deploy.log
                        gfarm_sasl.conf
                        gfhost.log
                        result.log


tests.table Format
------------------
Format:

    ID^NAME^CHANGES^EXPECT

Example:

    3^issuer NG^xoauth2_issuers: https://example/hoge^fail
    4^scope OK^xoauth2_scope: hpci^success


CHANGES Format
--------------
Multiple configuration changes are separated by ';'.

Example:

    xoauth2_scope: hpci hoge;
    xoauth2_user_claim: hpci.id

If a key already exists in init_gfarm_sasl.conf,
its value is replaced.

If the key does not exist,
a new line is appended to gfarm_sasl.conf.


Result Criteria
---------------
success test:
    - gfhost -l returns exit code 0
    - XOAUTH2 authentication is used

failure test:
    - gfhost -l returns non-zero


Notes
-----
run_test.sh temporarily adds:

    log_auth_verbose enable

to ~/.gfarm2rc
to verify XOAUTH2 authentication logs.

The setting is automatically removed when the script exits.


Using Another Environment
-------------------------
If another issuer URL, hostname configuration,
or sasl_user is used,
the following files may need to be modified.

init_gfarm_sasl.conf
    - xoauth2_issuers
    - xoauth2_scope
    - xoauth2_aud
    - xoauth2_user_claim

tests.table
    - issuer URLs used in test cases
    - sasl_user-dependent test entries
    - xoauth2_group_user definitions

deploy_gfarm_sasl.sh
    - deployment target hosts
    - passwordless ssh/scp configuration
    - sasl2 configuration path
