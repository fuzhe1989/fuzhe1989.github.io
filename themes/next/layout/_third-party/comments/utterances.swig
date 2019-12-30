{% if theme.utterances.enable %}
<script src="https://utteranc.es/client.js"
        repo="{{ theme.utterances.repo }}"
        issue-term="{{ theme.utterances.issue_term }}"
        label="Comment"
        theme="{{ theme.utterances.theme }}"
        crossorigin="anonymous"
        async>
</script>
{% endif %}