<?php
// $Id$

/**
 * @file
 * Import wizard for Drupal-to-Drupal migrations. Based on the form_example
 * wizard from http://drupal.org/project/examples.
 */

/**
 * Return the list of steps and their associated forms.
 *
 * @return $array
 */
function _migrate_drupal_import_steps() {
  return array(
    1 => array(
      'form' => 'migrate_drupal_credentials',
    ),
    2 => array(
      'form' => 'migrate_drupal_taxonomy',
    ),
    3 => array(
      'form' => 'migrate_drupal_users',
    ),
    4 => array(
      'form' => 'migrate_drupal_content_types',
    ),
    5 => array(
      'form' => 'migrate_drupal_fields',
    ),
  );
}

/**
 * The primary formbuilder function for the wizard form.
 *
 * This form has two defined submit handlers to process the different steps:
 *  - Previous: handles the way to get back one step in the wizard.
 *  - Next:     handles each step form submission,
 *
 * The third handler, the finish button handler, is the default form _submit
 * handler used to process the information.
 */
function migrate_drupal_import($form, &$form_state) {
  // Initialize a description of the steps for the wizard.
  if (empty($form_state['step'])) {
    $form_state['step'] = 1;

    // This array contains the function to be called at each step to get the
    // relevant form elements. It will also store state information for each
    // step.
    $form_state['step_information'] = _migrate_drupal_import_steps();
  }
  $step = &$form_state['step'];
  drupal_set_title(t('Drupal migration: Step @step', array('@step' => $step)));

  // Call the function named in $form_state['step_information'] to get the
  // form elements to display for this step.
  $form = $form_state['step_information'][$step]['form']($form, $form_state);

  // Show the 'previous' button if appropriate. Note that #submit is set to
  // a special submit handler, and that we use #limit_validation_errors to
  // skip all complaints about validation when using the back button. The
  // values entered will be discarded, but they will not be validated, which
  // would be annoying in a "back" button.
  if ($step > 1) {
    $form['prev'] = array(
      '#type' => 'submit',
      '#value' => t('Previous'),
      '#name' => 'prev',
      '#submit' => array('migrate_drupal_import_previous_submit'),
      '#limit_validation_errors' => array(),
    );
  }

  // Show the Next button only if there are more steps defined.
  if ($step < count($form_state['step_information'])) {
    // The Next button should be included on every step
    $form['next'] = array(
      '#type' => 'submit',
      '#value' => t('Next'),
      '#name' => 'next',
      '#submit' => array('migrate_drupal_import_next_submit'),
    );
  }
  else {
    // Just in case there are no more steps, we use the default submit handler of
    // the form wizard. When this button is clicked, the drupal_migrate_import_submit
    // handler will be called.
    $form['finish'] = array(
      '#type' => 'submit',
      '#value' => t('Finish'),
    );
  }

  // Include each validation function defined for the different steps.
  if (function_exists($form_state['step_information'][$step]['form'] . '_validate')) {
    $form['next']['#validate'] = array($form_state['step_information'][$step]['form'] . '_validate');
  }

  return $form;
}

/**
 * Submit handler for the "previous" button.
 * - Stores away $form_state['values']
 * - Decrements the step counter
 * - Replaces $form_state['values'] with the values from the previous state.
 * - Forces form rebuild.
 */
function migrate_drupal_import_previous_submit($form, &$form_state) {
  $current_step = &$form_state['step'];
  $form_state['step_information'][$current_step]['stored_values'] = $form_state['values'];
  if ($current_step > 1) {
    $current_step--;
    $form_state['values'] = $form_state['step_information'][$current_step]['stored_values'];
  }
  $form_state['rebuild'] = TRUE;
}

/**
 * Submit handler for the 'next' button.
 * - Saves away $form_state['values']
 * - Increments the step count.
 * - Replace $form_state['values'] from the last time we were at this page
 *   or with array() if we haven't been here before.
 * - Force form rebuild.
 *
 * @param unknown_type $form
 * @param unknown_type $form_state
 */
function migrate_drupal_import_next_submit($form, &$form_state) {
  $current_step = &$form_state['step'];
  $form_state['step_information'][$current_step]['stored_values'] = $form_state['values'];

  if ($current_step < count($form_state['step_information'])) {
    $current_step++;
    if (!empty($form_state['step_information'][$current_step]['stored_values'])) {
      $form_state['values'] = $form_state['step_information'][$current_step]['stored_values'];
    }
    else {
      $form_state['values'] = array();
    }
    $form_state['rebuild'] = TRUE;  // Force rebuild with next step.
    return;
  }
}

/**
 * Form for specifying where to obtain the Drupal content.
 */
function migrate_drupal_credentials($form, &$form_state) {
  $drush = variable_get('migrate_drupal_drush', '');
  if (!$drush) {
    if (user_access(MIGRATE_DRUPAL_ACCESS_CONFIGURE)) {
      $message = t('Drupal migration must be <a href="@config">configured</a> before use',
        array('@config' => url('admin/content/migrate_drupal/configure')));
    }
    else {
      $message = t('Drupal migration is not properly configured - please contact
        a site administrator');
    }
    $form['unconfigured'] = array(
      '#prefix' => '<div>',
      '#markup' => $message,
      '#suffix' => '</div>',
    );
    return $form;
  }

  $form['overview'] = array(
    '#prefix' => '<div>',
    '#markup' => t('External Drupal sites can be imported into Drupal using this form.
      Please provide the credentials for an account on the external Drupal site which
      has access to all content and account information. It is recommended that this not
      be the built-in admin account (user 1), but an explicitly created account with
      the proper permissions. TODO: Explain how to set up Services 3 and migrate_service
      on the external site.'),
    '#suffix' => '</div>',
  );

  $form['description'] = array(
    '#prefix' => '<div>',
    '#markup' => t('To import from an external Drupal site, enter your credentials here.'),
    '#suffix' => '</div>',
  );

  $form['domain'] = array(
    '#type' => 'textfield',
    '#title' => t('Domain of the external Drupal site'),
    '#description' => t('Enter the domain of the external Drupal site to import;
      for example, www.example.com.'),
    '#required' => TRUE,
  );

  $form['username'] = array(
    '#type' => 'textfield',
    '#title' => t('Username of the remote Drupal account'),
    '#description' => t(''),
    '#required' => TRUE,
  );

  $form['password'] = array(
    '#type' => 'password',
    '#title' => t('Password to the remote Drupal account'),
    '#description' => t(''),
    '#required' => TRUE,
  );

  return $form;
}

/**
 * Attempt to authenticate using the provided credentials.
 *
 * @param unknown_type $form
 * @param unknown_type $form_state
 */
function migrate_drupal_credentials_validate($form, &$form_state) {
  // Make sure we can connect
  $domain = $form_state['values']['domain'];
  // Session-based authentication for now
  $endpoint = $form_state['storage']['endpoint'] = 'http://' . $domain . '/admin/migrate';
  $username = $form_state['values']['username'];
  $password = $form_state['values']['password'];

  $data = array(
    'username' => $username,
    'password' => $password,
  );
  $data = http_build_query($data, '', '&');
  $options = array(
    'headers' => array(
      'Accept' => 'application/json',
    ),
    'method' => 'POST',
    'data' => $data
  );
  $response = drupal_http_request($endpoint . '/user/login', $options);
  if ($response->code == '200') {
    // Site exists, domain valid
    $data = json_decode($response->data);
    $form_state['storage']['http_options'] =
      array('headers' => array('Cookie' => $data->session_name . '=' . $data->sessid));
  }
  elseif ($response->code == '401') {
    form_set_error('username', t('Failed to login at !endpoint, message returned: !message',
      array('!endpoint' => $endpoint, '!message' => $response->error)));
  }
  else {
    form_set_error('domain', t('Could not connect to !url, response code !code, message returned: !message',
      array('!url' => $endpoint, '!code' => $response->code, '!message' => $response->error)));
  }
}

/**
 * Choose what source vocabularies to import into what destination vocabularies.
 */
function migrate_drupal_taxonomy($form, &$form_state) {
  $form['#tree'] = TRUE;
  $form['description'] = array(
    '#prefix' => '<div>',
    '#markup' => t('For each vocabulary on the source site, choose the destination
      site vocabulary to import its terms. You may also choose not to import a given
      vocabulary, or automatically create a new vocabulary of the same name if
      one does not already exist.'),
    '#suffix' => '</div>',
  );

  $url = $form_state['storage']['endpoint'] . '/taxonomy_vocabulary';
  $response = drupal_http_request($url, $form_state['storage']['http_options']);
  if ($response->code == '200') {
    $base_options = array(
      -1 => t('--Do not import--'),
      0 => t('--Create vocabulary--'),
    );
    $vocab_options = array();
    $local_vocabs = taxonomy_get_vocabularies();
    foreach ($local_vocabs as $vid => $vocab) {
      $vocab_options[$vid] = $vocab->name;
    }
    $data = json_decode($response->data);
    $header = array('source' => t('Source vocabulary'), 'destination' => t('Destination vocabulary'));
    foreach ($data as $vocab) {
      $options = $base_options + $vocab_options;
      // If we have a match on vocabulary name, default the mapping to that match
      // and remove the option to create a new vocabulary of that name.
      if ($vid = array_search($vocab->name, $vocab_options)) {
        $default_value = $vid;
        unset($options[0]);
      }
      else {
        $default_value = -1;
      }
      $form['vocab'][$vocab->vid] = array(
        '#type' => 'select',
        '#title' => $vocab->name,
        '#options' => $options,
        '#default_value' => $default_value,
      );
    }
  }
  else {
    form_set_error('description', t('Could not connect to !url', array('!url' => $url)));
  }
  return $form;
}

/**
 * Save the selected vocabulary mappings, as an array keyed by source vid and
 * containing the destination machine_name (bundle).
 */
function migrate_drupal_taxonomy_validate($form, &$form_state) {
  foreach ($form_state['values']['vocab'] as $source_vid => $dest_vid) {
    // New vocabulary requested, retrieve data on old one
    if ($dest_vid == 0) {
      $url = $form_state['storage']['endpoint'] . '/taxonomy_vocabulary/' . $source_vid;
      $response = drupal_http_request($url, $form_state['storage']['http_options']);
      if ($response->code == '200') {
        $vocab = json_decode($response->data);
        // Lack of a machine_name indicates a D6 source, generate the machine_name
        if (!isset($vocab->machine_name)) {
          $name = drupal_strtolower($vocab->name);
          $vocab->machine_name = preg_replace('/[^a-z0-9]+/', '_', $name);
        }
        unset($vocab->vid);
        if (taxonomy_vocabulary_save($vocab)) {
          $form_state['storage']['vocabs'][$source_vid] = $vocab->machine_name;
          drupal_set_message(t('New vocabulary !name created', array('!name' => $vocab->name)));
        }
        else {
          form_set_error('vocab][' . $source_vid, t('Failed to create vocabulary !name',
            array('!name' => $vocab->name)));
        }
      }
      else {
        form_set_error('description', t('Could not connect to !url', array('!url' => $url)));
      }
    }
    elseif ($dest_vid != -1) {
      $vocab = taxonomy_vocabulary_load($dest_vid);
      $form_state['storage']['vocabs'][$source_vid] = $vocab->machine_name;
    }
  }
}

/**
 * Form for specifying where to obtain the Drupal content.
 */
function migrate_drupal_users($form, &$form_state) {
  $form['description'] = array(
    '#prefix' => '<div>',
    '#markup' => t('Map old users/roles to new site'),
    '#suffix' => '</div>',
  );

  return $form;
}

/**
 * Form for specifying where to obtain the Drupal content.
 */
function migrate_drupal_content_types($form, &$form_state) {
  $form['description'] = array(
    '#prefix' => '<div>',
    '#markup' => t('Map old content types to new content types'),
    '#suffix' => '</div>',
  );

  return $form;
}

/**
 * Form for specifying where to obtain the Drupal content.
 */
function migrate_drupal_fields($form, &$form_state) {
  $form['description'] = array(
    '#prefix' => '<div>',
    '#markup' => t('Map old fields to new fields'),
    '#suffix' => '</div>',
  );

  return $form;
}

/**
 * Wizard form submit handler.
 *
 * @param unknown_type $form
 * @param unknown_type $form_state
 */
function migrate_drupal_import_submit($form, &$form_state) {
  try {
    $site = migrate_drupal_site($form_state['storage']['endpoint'], $form_state['storage']['http_options']['headers']['Cookie']);
  }
  catch (Exception $e) {
    drupal_set_message($e->getMessage());
    return;
  }

  // Register a Term migration for each vocabulary mapping
  $vocabs = $form_state['storage']['vocabs'];
  foreach ($vocabs as $source_vid => $bundle) {
    $arguments = array('endpoint' => $form_state['storage']['endpoint'],
      'source_vid' => $source_vid, 'bundle' => $bundle);
    Migration::registerMigration('DrupalTerm', "DrupalTerm$source_vid", $arguments);
  }
}
