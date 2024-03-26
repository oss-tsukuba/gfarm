CREATE USER 'replica'@'%' IDENTIFIED BY 'gfarm123';
RESET MASTER;
GRANT REPLICATION SLAVE ON *.* TO 'replica'@'%';
CHANGE MASTER TO
  MASTER_HOST = 'jwt-server',
  MASTER_PORT = 3306,
  MASTER_USER = 'replica',
  MASTER_PASSWORD = 'gfarm123',
  MASTER_CONNECT_RETRY = 10,
  MASTER_USE_GTID = slave_pos;
START SLAVE;