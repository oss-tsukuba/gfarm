DROP SCHEMA IF EXISTS gfarm;
CREATE SCHEMA gfarm;
USE gfarm;

DROP TABLE IF EXISTS errors;

CREATE TABLE `errors` (
  `id` int NOT NULL AUTO_INCREMENT,
  `user` varchar(20) NOT NULL,
  `date` timestamp NULL,
  `type` int DEFAULT '0',
  `ip_addr` varchar(256) DEFAULT NULL,
  `hostname` varchar(256) DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `id` (`id`)
);

DROP TABLE IF EXISTS tokens;

CREATE TABLE `tokens` (
  `user` varchar(20) NOT NULL,
  `audience` varchar(20) NOT NULL,
  `access_token` text,
  `refresh_token` text,
  `iv` varchar(128) NOT NULL,
  PRIMARY KEY (`user`,`audience`)
);

DROP TABLE IF EXISTS token_time;

CREATE TABLE `token_time` (
  `user` varchar(20) NOT NULL,
  `login_at` bigint DEFAULT 0,
  `logout_at` bigint DEFAULT 0,
  PRIMARY KEY (`user`)
);

DROP TABLE IF EXISTS issues;

CREATE TABLE IF NOT EXISTS `issues` (
  `id` int NOT NULL AUTO_INCREMENT,
  `user` varchar(20) NOT NULL,
  `date` timestamp NULL,
  `ip_addr` varchar(256) DEFAULT NULL,
  `hostname` varchar(256) DEFAULT NULL,
  `type` int DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `id` (`id`)
);

