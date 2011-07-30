<?php
// $Id$

/**
 * @file
 *
 * Web UI for migrating external Drupal sites to Drupal.
 */

/**
 * Submit callback for the Drupal import form.
 */
function migrate_drupal_import_form_submit($form, &$form_state) {
  // If endpoint is domain alone, default to /services/rest
  $endpoint = $form_state['values']['endpoint'];

  // Use username/password to obtain token
  $username = $form_state['values']['username'];
  $password = $form_state['values']['password'];
  $token = 'dummy';

  // TODO: Should go through multi-step process:
  // 1. Choose what content items to migrate:
  //    a. List of content types, map to destination content types (defaulting where
  //       machine names match), whether to migrate comments for each
  //    b. List of vocabularies, map to destination vocabularies (defaulting where
  //       names match, machine_name if any)
  //    c. List of roles, map to destination roles (defaulting where names match) -
  //       migrate only users in the specified roles
  //    d. And, for each of the above, option to clone the structure from the source site?
  // 2. Map fields (TBD)
  try {
    $site = migrate_drupal_site($endpoint, $token);
  }
  catch (Exception $e) {
    drupal_set_message($e->getMessage());
    return;
  }

  // Import each migration in order, until done or time is running out
  $spawned = FALSE;
  foreach ($site->migrations() as $migration) {
    $result = $migration->processImport();
    if ($result == MigrationBase::RESULT_INCOMPLETE) {
      $drush = variable_get('migrate_drupal_drush', '');
      drupal_set_message(t('The site is too large to completely import immediately -
          the rest of the import will be run in the background and you will receive an email
          when it is complete'));
      $uri = 'http://' . $_SERVER['HTTP_HOST'];
      $log_file = '/tmp/' . $filename . '.import.log';
      $command = "$drush --uri=$uri migrate-drupal-import $endpoint >$log_file 2>&1 &";
      exec($command);
      $spawned = TRUE;
      break;
    }
  }
  if (!$spawned) {
    drupal_set_message(t('File %filename successfully uploaded and imported',
      array('%filename' => $filename)));
  }
}

/**
 * Menu callback: Returns a page for reviewing Drupal migrations.
 */
function migrate_drupal_review() {
  drupal_set_title(t('Drupal migrations'));
  return drupal_get_form('migrate_drupal_review_form');
}

/**
 * Form for reviewing WordPress migrations.
 */
function migrate_drupal_review_form($form, &$form_state) {
  if (isset($form_state['values']['operation']) &&
      ($form_state['values']['operation'] == 'rollback' || $form_state['values']['operation'] == 'clear')) {
    return migrate_drupal_rollback_confirm($form, $form_state, array_filter($form_state['values']['sites']));
  }
  $form['overview'] = array(
    '#prefix' => '<p>',
    '#markup' => t('These are the external Drupal sites which you have imported into your
      Drupal site. For each component of the site, the number of imported items is
      displayed.'),
    '#suffix' => '</p>',
  );

  $header = array(
    'endpoint' => array('data' => t('Endpoint')),
    'status' => array('data' => t('Status')),
    'MigrateDrupalTerm' => array('data' => t('Terms')),
    'MigrateDrupalNode' => array('data' => t('Nodes')),
    'MigrateDrupalComment' => array('data' => t('Comments')),
  );
  $rows = array();
  $sites = call_user_func(array(migrate_drupal_site_class(), 'sites'));
  foreach ($sites as $site) {
    $classes = array_flip($site->migrationClasses());
    $row['endpoint'] = $site->getEndpoint();
    $row['status'] = t('Complete');
    foreach ($site->migrations() as $migration) {
      $class = $classes[get_class($migration)];
      $row[$class] = $migration->importedCount();
      $status = $migration->getStatus();
      if ($status == MigrationBase::STATUS_IMPORTING) {
        $row['status'] = t('Importing');
      }
      elseif ($status == MigrationBase::STATUS_ROLLING_BACK) {
        $row['status'] = t('Deleting');
      }
    }
    $rows[$site->getEndpoint()] = $row;
  }

  $form['sites'] = array(
    '#type' => 'tableselect',
    '#header' => $header,
    '#options' => $rows,
    '#empty' => t('No external Drupal sites have been migrated into your Drupal site'),
  );

  // Build the 'Update options' form.
  $form['options'] = array(
    '#type' => 'fieldset',
    '#title' => t('Update options'),
    '#attributes' => array('class' => array('container-inline')),
  );
  $options = array(
    'rollback' => t('Remove imported content'),
    'clear' => t('Remove migration bookkeeping'),
  );
  $form['options']['description'] = array(
    '#prefix' => '<div>',
    '#markup' => t('The Drupal migration process does considerable bookkeeping,
      tracking how the original external Drupal content maps to the imported Drupal content.
      This bookkeeping allows you to easily back out the migration, restoring your
      Drupal site to its initial state; once you are satisfied with the imported
      content, you may remove the bookkeeping overhead.
      <ul><li><strong>Remove imported content</strong> will restore
      your Drupal site to its state before the external Drupal sites were
      imported, deleting all imported content as well as the bookkeeping overhead.</li>
      <li><strong>Remove bookkeeping only</strong> will remove the bookkeeping
      overhead, retaining all imported content.</li></ul>'),
    '#postfix' => '</div>',
  );
  $form['options']['operation'] = array(
    '#type' => 'select',
    '#title' => t('Operation'),
    '#title_display' => 'invisible',
    '#options' => $options,
  );
  $form['options']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Update'),
    '#validate' => array('migrate_drupal_review_validate'),
    '#submit' => array('migrate_drupal_review_submit'),
  );

  return $form;
}

/**
 * Validate callback for the Drupal review form.
 */
function migrate_drupal_review_validate($form, &$form_state) {
  // Error if there are no items to select.
  if (!is_array($form_state['values']['sites']) || !count(array_filter($form_state['values']['sites']))) {
    form_set_error('', t('No items selected.'));
  }
}

/**
 * Submit callback for the Drupal review form.
 */
function migrate_drupal_review_submit($form, &$form_state) {
  // We need to rebuild the form to go to a second step (confirm site rollback)
  $form_state['rebuild'] = TRUE;
}

function migrate_drupal_rollback_confirm($form, &$form_state, $sites) {
  $operation = $form_state['values']['operation'];

  $form['sites'] = array('#prefix' => '<ul>', '#suffix' => '</ul>', '#tree' => TRUE);
  // array_filter returns only elements with TRUE values
  foreach ($sites as $endpoint) {
    $form['sites'][$endpoint] = array(
      '#type' => 'hidden',
      '#value' => $endpoint,
      '#prefix' => '<li>',
      '#suffix' => check_plain($endpoint) . "</li>\n",
    );
  }
  $form['operation'] = array('#type' => 'hidden', '#value' => $operation);
  $form['#submit'][] = 'migrate_drupal_rollback_confirm_submit';
  if ($operation == 'rollback') {
    $confirm_question = format_plural(count($sites),
                                    'Are you sure you want to remove all imported content for this site?',
                                    'Are you sure you want to remove all imported content for these sites?');
  }
  else {
    $confirm_question = format_plural(count($sites),
                                    'Are you sure you want to remove all migration bookkeeping for this site?',
                                    'Are you sure you want to remove all migration bookkeeping for these sites?');
  }
  return confirm_form($form,
                    $confirm_question,
                    'admin/content/migrate_drupal/review', t('This action cannot be undone.'),
                    t('Delete'), t('Cancel'));
}

function migrate_drupal_rollback_confirm_submit($form, &$form_state) {
  if ($form_state['values']['confirm']) {
    $sites = array_keys($form_state['values']['sites']);
    foreach ($sites as $endpoint) {
      // TODO: Batch API
      // Rollback migrations for this domain
      $site = migrate_drupal_site($endpoint);
      $migrations = array_reverse($site->migrations());
      $success = TRUE;
      foreach ($migrations as $migration) {
        // Only rollback content for the rollback operation
        if ($form_state['values']['operation'] == 'clear') {
          // Remove map/message tables, and migrate_status table entry
          Migration::deregisterMigration($migration->getMachineName());
          $success = TRUE;
        }
        else {
          // If not currently idle, stop it before attempting rollback
          if ($migration->getStatus() != MigrationBase::STATUS_IDLE) {
            $migration->stopProcess();
            // Give it a little time to react to the stop request
            $count = 5;
            while ($count && $migration->getStatus() == MigrationBase::STATUS_STOPPING) {
              sleep(2);
              $count--;
            }
            if ($migration->getStatus() == MigrationBase::STATUS_STOPPING) {
              // At this point, assume it's stuck and reset the status so we can continue
              $migration->resetStatus();
            }
          }
          $result = $migration->processRollback();
          if ($result == MigrationBase::RESULT_INCOMPLETE) {
            $drush = variable_get('migrate_drupal_drush', '');
            if (!$drush) {
              if (user_access(MIGRATE_DRUPAL_ACCESS_CONFIGURE)) {
                $message = t('The blog was too large to delete through the browser - please
                      <a href="@config">configure drush</a> so the deletion process may be
                      run in the background.',
                array('@config' => url('admin/content/wordpress/configure')));
              }
              else {
                $message = t('The blog was too large to delete through the browser - please
                    contact a site administrator to properly configure the site for
                    background deletion.');
              }
              drupal_set_message($message);
              break;
            }
            drupal_set_message(t('The blog is too large to completely delete immediately -
                the rest of the deletion will be run in the background.'));
            $uri = 'http://' . $_SERVER['HTTP_HOST'];
            $log_file = '/tmp/' . basename($blog->getFilename()) . '.rollback.log';
            $destination = $blog->getFilename();
            $command = "$drush --uri=$uri wordpress-migrate-rollback $destination >$log_file 2>&1 &";
            exec($command);
            $success = FALSE;
            break;
          }
          elseif ($result == MigrationBase::RESULT_COMPLETED) {
            // Remove map/message tables, and migrate_status table entry
            Migration::deregisterMigration($migration->getMachineName());
          }
          else {
            drupal_set_message(t('Failed to complete rollback, status=!status',
              array('!status' => $result)));
            $success = FALSE;
            break;
          }
        }
      }

      if ($success) {
        // Clear wordpress_migrate table entry
        db_delete('wordpress_migrate')
          ->condition('blog_url', $blog_url)
          ->execute();

        // Delete WXR file
        file_unmanaged_delete($filename);

        // Delete photo gallery
        if (module_exists('media_gallery')) {
          global $user;
          $blog_title = t("@name's blog", array('@name' => format_username($user)));
          $gallery_nid = db_select('node', 'n')
            ->fields('n', array('nid'))
            ->condition('type', 'media_gallery')
            ->condition('title', $blog_title)
            ->execute()
            ->fetchField();
          if ($gallery_nid) {
            node_delete($gallery_nid);
          }
        }
      }
    }
    if ($success) {
      $count = count($form_state['values']['blogs']);
      watchdog('content', 'Deleted @count WordPress blogs.', array('@count' => $count));
      drupal_set_message(format_plural($count, 'Deleted 1 WordPress blog.',
        'Deleted @count WordPress blogs.'));
    }
  }
  $form_state['redirect'] = 'admin/content/wordpress/review';
}

/**
 * Menu callback: Returns a page for configuring WordPress migrations.
 */
function migrate_drupal_configure() {
  drupal_set_title(t('WordPress configuration'));
  return drupal_get_form('migrate_drupal_configure_form');
}

/**
 * Form for configuring WordPress migrations.
 */
function migrate_drupal_configure_form($form, &$form_state) {
  $form['migrate_drupal_drush'] = array(
    '#type' => 'textfield',
    '#title' => t('Location of drush command on server'),
    '#default_value' => variable_get('migrate_drupal_drush', ''),
    '#description' => t('Larger site need to be imported by spawning a drush
      command. Please enter the full path of the drush command on the server
      to enable this functionality.'),
  );
/*
  // Select destination node type(s)
  $node_types = node_type_get_types();
  $options = array();
  foreach ($node_types as $node_type => $info) {
    $options[$node_type] = $info->name;
  }

  if (isset($options['blog'])) {
    $default = 'blog';
  }
  else {
    $default = '';
  }

  $form['migrate_drupal_post_type'] = array(
    '#type' => 'select',
    '#title' => t('Type of node to hold WordPress posts'),
    '#default_value' => variable_get('migrate_drupal_post_type', $default),
    '#options' => $options,
    '#description' => t(''),
  );

  if (isset($options['page'])) {
    $default = 'page';
  }
  else {
    $default = '';
  }

  $form['migrate_drupal_page_type'] = array(
    '#type' => 'select',
    '#title' => t('Type of node to hold WordPress pages'),
    '#default_value' => variable_get('migrate_drupal_page_type', $default),
    '#options' => $options,
    '#description' => t(''),
  );
*/
  // Select default text format for bodies etc.
  $options = array();
  foreach (filter_formats() as $format_id => $format) {
    $options[$format_id] = $format->name;
  }
  $form['migrate_drupal_text_format'] = array(
    '#type' => 'select',
    '#title' => t('Format for text fields'),
    '#default_value' => variable_get('migrate_drupal_text_format', 'filtered_html'),
    '#options' => $options,
    '#description' => t(''),
  );

  // TODO: Select user to own blog
/*
  // Select vocabularies for categories and tags
  // TODO: Get only those attached to destination content types
  $vocabs = taxonomy_vocabulary_get_names();
  $options = array('' => t('Do not import'));
  foreach ($vocabs as $machine_name => $vocab) {
    $options[$machine_name] = $vocab->name;
  }

  $form['migrate_drupal_tag_vocabulary'] = array(
    '#type' => 'select',
    '#title' => t('Vocabulary for WordPress tags'),
    '#default_value' => variable_get('migrate_drupal_tag_vocabulary', ''),
    '#options' => $options,
    '#description' => t('Choose the vocabulary to hold WordPress tags.'),
  );

  $form['migrate_drupal_category_vocabulary'] = array(
    '#type' => 'select',
    '#title' => t('Vocabulary for WordPress categories'),
    '#default_value' => variable_get('migrate_drupal_category_vocabulary', ''),
    '#options' => $options,
    '#description' => t('Choose the vocabulary to hold WordPress categories.'),
  );
*/
  // TODO: Select mechanism for running import (Batch API, cron, spawned process...)

  $form['migrate_drupal_notification_subject'] = array(
    '#type' => 'textfield',
    '#title' => t('Subject for email notifications'),
    '#default_value' => variable_get('migrate_drupal_notification_subject',
      t('Drupal import status')),
    '#description' => t(''),
  );

  $form['migrate_drupal_notification_body'] = array(
    '#type' => 'textarea',
    '#title' => t('Body for email notifications of import success'),
    '#default_value' => variable_get('migrate_drupal_notification_body',
      t("Your Drupal import is complete! Any messages generated are below.\n\n!output")),
  );

  $form['migrate_drupal_notification_failure_body'] = array(
    '#type' => 'textarea',
    '#title' => t('Body for email notifications of import failure'),
    '#default_value' => variable_get('migrate_drupal_notification_failure_body',
      t('Your Drupal import failed. Any messages generated are below.\n\n!output')),
  );

  // TODO: For most of the above, indicate whether they can be overridden per import

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save configuration changes'),
  );

  return $form;
}

/**
 * Submit callback for the WordPress configure form.
 */
function migrate_drupal_configure_form_submit($form, &$form_state) {
  // TODO: Verify drush command file exists
  variable_set('migrate_drupal_drush', $form_state['values']['migrate_drupal_drush']);

//  variable_set('migrate_drupal_post_type', $form_state['values']['migrate_drupal_post_type']);
  variable_set('migrate_drupal_text_format', $form_state['values']['migrate_drupal_text_format']);
//  variable_set('migrate_drupal_page_type', $form_state['values']['migrate_drupal_page_type']);
//  variable_set('migrate_drupal_tag_vocabulary', $form_state['values']['migrate_drupal_tag_vocabulary']);
//  variable_set('migrate_drupal_category_vocabulary', $form_state['values']['migrate_drupal_category_vocabulary']);
  variable_set('migrate_drupal_notification_subject', $form_state['values']['migrate_drupal_notification_subject']);
  variable_set('migrate_drupal_notification_body', $form_state['values']['migrate_drupal_notification_body']);
  variable_set('migrate_drupal_notification_failure_body', $form_state['values']['migrate_drupal_notification_failure_body']);
  drupal_set_message(t('Drupal configuration changes saved.'));
}
