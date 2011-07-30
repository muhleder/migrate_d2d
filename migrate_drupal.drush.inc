<?php
// $Id: wordpress_migrate.drush.inc,v 1.1.2.5 2011/02/07 16:11:35 mikeryan Exp $

/**
 * @file
 * Drush support for the wordpress_migrate module
 */

/**
 * Implementation of hook_drush_help().
 */
function migrate_drupal_drush_help($section) {
  switch ($section) {
    case 'drush:migrate-drupal-import':
      return dt('Import a Drupal site');
    case 'drush:migrate-drupal-rollback':
      return dt('Rollback a Drupal site');
  }
}

/**
 * Implementation of hook_drush_command().
 */
function migrate_drupal_drush_command() {
  static $commands = FALSE;

  $items['migrate-drupal-import'] = array(
    'description' => 'Import a Drupal site',
    'arguments' => array(
      'endpoint' => 'Endpoint on remote Drupal site to import',
    ),
    'examples' => array(
      'migrate-drupal-import http://example.com/services/rest' => 'Import site data',
    ),
    'drupal dependencies' => array('migrate'),
  );
  $items['migrate-drupal-rollback'] = array(
    'description' => 'Rollback a Drupal site',
    'arguments' => array(
      'endpoint' => 'Endpoint on remote Drupal site to import',
    ),
    'examples' => array(
      'migrate-drupal-rollback http://example.com/services/rest' => 'Rollback site data',
    ),
    'drupal dependencies' => array('migrate'),
  );
  return $items;
}

function drush_migrate_drupal_import($endpoint) {
  // Capture non-informational output for mailing
  ob_start();
  try {
    $site = migrate_drupal_site($endpoint);
    $mail_key = 'import_complete';
    $migrations = $site->migrations();
    foreach ($migrations as $migration) {
      $result = $migration->processImport();
      if ($result != MigrationBase::RESULT_COMPLETED) {
        $mail_key = 'import_failure';
        break;
      }
    }
  }
  catch (Exception $e) {
    drush_log($e->getMessage(), 'error');
    $mail_key = 'import_failure';
  }

  // Notify user
  $account = user_load($site->getUid());
  $params['account'] = $account;
  $params['output'] = ob_get_contents();
  ob_end_flush();
  drupal_mail('migrate_drupal', $mail_key, $account->mail,
    user_preferred_language($account), $params);
}

function drush_migrate_drupal_rollback($endpoint) {
  try {
    $site = migrate_drupal_site($endpoint);
    $migrations = array_reverse($site->migrations());
    $incomplete = FALSE;
    foreach ($migrations as $migration) {
      $result = $migration->processRollback();
      if ($result != MigrationBase::RESULT_COMPLETED) {
        $incomplete = TRUE;
        drush_log(dt('!title rollback failed to complete', array('!title' => $site->getTitle())));
        break;
      }
      // Remove map/message tables, and migrate_status table entry
      Migration::deregisterMigration($migration->getMachineName());
    }

    if (!$incomplete) {
      // Clear wordpress_migrate table entry
      db_delete('migrate_drupal')
        ->condition('endpoint', $endpoint)
        ->execute();

      // Delete photo gallery
      if (module_exists('media_gallery')) {
        $account = user_load($site->getUid());
        $blog_title = t("@name's blog", array('@name' => format_username($account)));
        $gallery_nid = db_select('node', 'n')
          ->fields('n', array('nid'))
          ->condition('type', 'media_gallery')
          ->condition('title', $site->getTitle())
          ->execute()
          ->fetchField();
        if ($gallery_nid) {
          node_delete($gallery_nid);
        }
      }
    }
  }
  catch (Exception $e) {
    drush_log($e, 'error');
  }
}
