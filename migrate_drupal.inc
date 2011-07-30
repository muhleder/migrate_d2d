<?php
// $Id: wordpress.inc,v 1.1.2.10 2011/02/07 20:50:51 mikeryan Exp $

/**
 * @file
 * Implementation of migration from external Drupal sites into Drupal
 */
abstract class DrupalMigration extends DynamicMigration {

  protected $site;

  public function getEndpoint() {
    return $this->site->getEndpoint();
  }

  public function getToken() {
    return $this->site->getToken();
  }

  /**
   * Constructor - general setup for WordPress migrations.
   *
   * @param array $arguments
   *  'filename' => WXR file managed by this migration
   */
  public function __construct(array $arguments) {
    if (!isset($arguments['endpoint'])) {
      throw new Exception(t('Endpoint is a required parameter for Drupal migrations'));
    }

    $this->site = migrate_drupal_site($arguments['endpoint']);
    parent::__construct($arguments);
  }

  /**
   * Suppress normal migrate module output in the browser. In drush, use
   * drush_print instead of drush_log to capture output (non-informational) for email.
   */
  public function showMessage($message, $level = MigrationBase::MESSAGE_ERROR) {
    if (function_exists('drush_print') && $level != 'completed') {
      drush_print($message);
    }
  }
}

class DrupalSite {
  protected $endpoint;
  public function getEndpoint() {
    return $this->endpoint;
  }
  protected $token;
  public function getToken() {
    return $this->token;
  }
  protected $uid;
  public function getUid() {
    return $this->uid;
  }

  public function __construct($endpoint, $token = NULL) {
    $this->endpoint = $endpoint;
    $row = db_select('migrate_drupal', 'md')
           ->fields('md', array('endpoint', 'token', 'uid'))
           ->condition('endpoint', $this->endpoint)
           ->execute()
           ->fetchObject();
    if ($row) {
      // Token mismatch - assume we're reauthenticating and update to the new one
      if (!empty($token) && $token != $row->token) {
        db_update('migrate_drupal')
        ->fields(array('token' => $token))
        ->condition('endpoint', $this->endpoint)
        ->execute();
        $this->token = $token;
      }
      else {
        $this->token = $row->token;
      }
      $this->uid = $row->uid;
    }
    else {
      global $user;
      $this->uid = $user->uid;
      $this->token = $token;
      db_insert('migrate_drupal')
        ->fields(array(
          'endpoint' => $this->endpoint,
          'token' => $this->token,
          'uid' => $this->uid,
        ))
        ->execute();
    }
  }

  /**
   * The implemented Drupal migration classess, in the order they should be run.
   */
  public function migrationClasses() {
    return array(
      'DrupalTerm' => 'DrupalTerm',
    );
  }

  public function migrations() {
    $migrations = array();
    foreach ($this->migrationClasses() as $class) {
      $result = db_select('migrate_status', 'ms')
                ->fields('ms', array('machine_name'))
                ->condition('class_name', $class)
                ->execute();
      foreach ($result as $row) {
        $migrations[] = MigrationBase::getInstance($row->machine_name);
      }
    }
    return $migrations;
  }

  static public function sites() {
    $sites = array();
    $result = db_select('drupal_migrate', 'dm')
               ->fields('dm', array('endpoint'))
               ->execute();
    foreach ($result as $row) {
      $sites[] = drupal_migrate_site($row->endpoint);
    }
    return $sites;
  }
}
