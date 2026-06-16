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

tests.table.in
    Template used to generate tests.table

tests.table
    Generated test case definitions

init_gfarm_sasl.conf.in
    Template used to generate init_gfarm_sasl.conf

gfarm_sasl.conf
    Generated from init_gfarm_sasl.conf during test execution

host.sh
    Host list definition

gfarm_sasl.conf
    Generated test configuration file


How to Run
----------
Obtain a JWT token and enable XOAUTH2 authentication
before executing the tests.

Example:

    % jwt-agent -s https://jwt-server/ -l user1

    % authconfig sasl.xoauth2

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
Currently, this test suite is primarily intended for the
docker/dev environment.

Some commands and assumptions used by the test scripts
are still specific to docker/dev, and execution in other
environments is not officially supported or validated.

run_test.sh verifies that XOAUTH2 authentication is
already working before executing the test cases.

Therefore, when using an environment different from the
default docker/dev setup, XOAUTH2 authentication must be
configured and validated in advance.

Typical setup steps are:

1. Configure the SASL plugin and client settings.

       gfarm.conf
       gfarm-client.conf

   The deploy_gfarm_sasl.sh script can be used to deploy
   the SASL plugin configuration to all hosts defined in
   host.sh.

2. Register the XOAUTH2 user mapping.

       gfuser -A user1 SASL hoge1

   The -A option associates an authentication-method
   specific user identifier with a global Gfarm user.

3. Configure the SASL user name.

       vi ~/.gfarm2rc.sasl.xoauth2

   Example:

       sasl_user "hoge1"

4. Enable XOAUTH2 authentication.

       authconfig sasl.xoauth2

5. Verify that XOAUTH2 authentication works.

       gfhost -l

run_test.sh starts only when XOAUTH2 authentication is
already functioning successfully.